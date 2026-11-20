// platform/kos/audio_kos.cpp - the AICA sound backend (slice 3D-7, docs/NEXT_SLICE_SOUND_SAVE.md 5.1, 5.2, 13.1):
// the eng::snd_* commands of src/engine.h on KallistiOS. Every KOS call here was read in the installed KOS 2.3.0
// (git 9ba0350f): dc/sound/stream.h + sound/snd_stream.c, dc/sound/sfxmgr.h + sound/snd_sfxmgr.c, kos/thread.h,
// kos/mutex.h, kos/fs.h.
//
// Effects: each .adp's ADPCM data goes into sound RAM once, at boot (snd_sfx_load_raw_buf, 4 bits); playing one is a
//   single AICA command on one of 8 channels reserved at boot, one per voice of the audio logic (snd_sfx_play_chn:
//   a new sound on a channel replaces the old one).
// Music: one mono ADPCM stream (snd_stream_start_adpcm), fed from the track's bytes where they lie in the romdisk
//   (fs_mmap: the romdisk is in RAM). A thread of its own owns the stream: it applies the game's latest request
//   (start / stop / volume) and polls every 8 ms, so the music keeps playing through loads. The game thread only
//   writes a small mailbox under a mutex that is never held across a KOS call.
//   The stream callback always hands KOS a full, 32-byte aligned block copied from the track (wrapping at the loop
//   point inside the block), so buffer positions stay aligned whatever the track's length, and the chip's decoder
//   sees one continuous code sequence: the .adp loop seams (tools/audio/adpcm.py) do the rest.
//
// Kill switches (makefile, TRIAGE): NO_AUDIO=1 - this backend refuses to boot (slice 3D-6's silence);
//   NO_MUSIC=1 - effects only (no stream, no thread).
#include <kos.h>
#include <dc/sound/sound.h>      // snd_mem_available: kos.h pulls in stream.h and sfxmgr.h, not this one
#include <stdint.h>
#include <string.h>
#include "../../src/adpcm.h"
#include "../../src/engine.h"
#include "audio_kos.h"

namespace {

const int VOICES = 8, MAX_SFX = 48, MAX_TRACKS = 12;
const int STREAM_BYTES = 16384;                    // per KOS: the stream's ring in sound RAM (32,768 samples: 1.5 s at 22,050 Hz)
const int HALF = STREAM_BYTES / 2;                 // KOS never asks for more than half the ring at once

struct Track { const uint8_t* data; uint32_t bytes, loop_byte, rate; bool loops, ok; };

bool     g_on = false;
sfxhnd_t g_sfx[MAX_SFX];
int      g_nsfx = 0;
int      g_chn[VOICES];
Track    g_track[MAX_TRACKS];
int      g_ntracks = 0;
SndKosStats g_stats;

// ---- the mailbox: written by the game thread, read by the music thread
struct Request { uint32_t gen; int track; bool loop; int vol; };
mutex_t g_mx = MUTEX_INITIALIZER;
Request g_req = { 0, -1, false, 0 };

// ---- owned by the music thread
snd_stream_hnd_t g_hnd = SND_STREAM_INVALID;
const Track* g_cur = nullptr;
uint32_t g_pos = 0;                                // the next byte of g_cur to hand out
bool g_loop = false;
alignas(32) uint8_t g_block[2][HALF];              // two blocks, alternating: KOS copies one to sound RAM by DMA while
int g_block_i = 0;                                 // the next is being filled

// Maps a romdisk file and reads its .adp header. The pointer stays valid after the file is closed (romdisk files
// are part of the program image).
bool map_adp(const char* path, const uint8_t** data, adpcm::Header* h) {
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) return false;
    const uint8_t* base = (const uint8_t*)fs_mmap(f);
    fs_close(f);
    if (!base || !adpcm::parse_header(base, h) || h->format != adpcm::FMT_ADPCM) return false;
    *data = base + adpcm::HEADER_BYTES;
    return true;
}

#if !defined(NO_MUSIC)
// KOS asks for `need` bytes (snd_stream_fill passes bytes, whatever the header's comment says) and takes what `got`
// reports. Always a full block: the track's bytes, wrapped at the loop point; after the end of a track that does
// not loop, code pairs +1/-1 (0x80: the decoder idles around zero until the game stops the stream).
void* music_data(snd_stream_hnd_t, int need, int* got) {
    g_block_i ^= 1;
    uint8_t* out = g_block[g_block_i];
    const int n = need > HALF ? HALF : (need < 0 ? 0 : need);
    int done = 0;
    const Track* t = g_cur;
    while (t && done < n) {
        if (g_pos >= t->bytes) {
            if (!g_loop || t->loop_byte >= t->bytes) break;
            g_pos = t->loop_byte;
        }
        const uint32_t left = t->bytes - g_pos;
        const int k = (uint32_t)(n - done) < left ? n - done : (int)left;
        memcpy(out + done, t->data + g_pos, (size_t)k);
        g_pos += (uint32_t)k;
        done += k;
    }
    if (done < n) memset(out + done, 0x80, (size_t)(n - done));
    *got = n;
    return out;
}

void* music_thread(void*) {
    uint32_t applied_gen = 0;
    int applied_vol = -1;
    bool playing = false;
    uint64_t last_poll = timer_ms_gettime64();
    for (;;) {
        mutex_lock(&g_mx);
        const Request r = g_req;
        mutex_unlock(&g_mx);
        if (r.gen != applied_gen) {
            applied_gen = r.gen;
            if (playing) { snd_stream_stop(g_hnd); playing = false; g_stats.stream_ms = 0; }
            if (r.track >= 0 && r.track < g_ntracks && g_track[r.track].ok) {
                g_cur = &g_track[r.track];
                g_pos = 0;
                g_loop = r.loop && g_cur->loops;
                // Queued start: KOS's start command sets the channel to full volume; with queueing on, the start and
                // the volume that follows reach the AICA together (a fade-in must not open with a full-level click).
                snd_stream_start_adpcm(g_hnd, g_cur->rate, 0);
                snd_stream_volume(g_hnd, r.vol);
                snd_stream_queue_go(g_hnd);
                applied_vol = r.vol;
                playing = true;
                ++g_stats.starts;
                g_stats.stream_ms = (int)((uint64_t)STREAM_BYTES * 2u * 1000u / g_cur->rate);
                last_poll = timer_ms_gettime64();
            }
        } else if (playing && r.vol != applied_vol) {
            snd_stream_volume(g_hnd, r.vol);
            applied_vol = r.vol;
        }
        if (playing) {
            const uint64_t now = timer_ms_gettime64();
            if ((uint32_t)(now - last_poll) > g_stats.poll_gap_ms) g_stats.poll_gap_ms = (uint32_t)(now - last_poll);
            last_poll = now;
            if (snd_stream_poll(g_hnd) < 0) ++g_stats.poll_errors;
        }
        thd_sleep(8);
    }
    return nullptr;
}
#endif

}  // namespace

SndKosStats snd_kos_stats() {
    const SndKosStats s = g_stats;
    g_stats.poll_gap_ms = 0;
    return s;
}

namespace eng {

bool snd_boot(const SndBank& bank) {
#if defined(NO_AUDIO)
    (void)bank;
    boot_log("audio: NO_AUDIO build");
    return false;
#else
    if (g_on) return true;                                         // boot only, once
    if (bank.nsfx > MAX_SFX || bank.nmusic > MAX_TRACKS) { boot_log("audio: the bank is larger than the backend's tables"); return false; }
    // mono streams only; KOS allocates one bounce buffer of this size and brings the AICA driver up (snd_init)
    if (snd_stream_init_ex(1, STREAM_BYTES) < 0) { boot_log("audio: snd_stream_init_ex failed"); return false; }
    char msg[160];
    bool ok = true;
    for (int i = 0; i < bank.nsfx; ++i) {
        const uint8_t* data = nullptr;
        adpcm::Header h;
        g_sfx[i] = SFXHND_INVALID;
        if (map_adp(bank.sfx_path[i], &data, &h) && h.samples <= 65534u)
            g_sfx[i] = snd_sfx_load_raw_buf((char*)data, (h.samples + 1u) / 2u, h.rate, 4, 1);
        if (g_sfx[i] == SFXHND_INVALID) { snprintf(msg, sizeof msg, "audio: effect %s did not load", bank.sfx_path[i]); dbg_print(msg); ok = false; }
    }
    g_nsfx = bank.nsfx;
    for (int v = 0; v < VOICES; ++v) { g_chn[v] = snd_sfx_chn_alloc(); if (g_chn[v] < 0) ok = false; }
    for (int i = 0; i < bank.nmusic; ++i) {
        adpcm::Header h;
        Track& t = g_track[i];
        t.ok = map_adp(bank.music_path[i], &t.data, &h) && (h.samples & 1u) == 0 && (h.loop_start & 1u) == 0;
        if (!t.ok) { snprintf(msg, sizeof msg, "audio: track %s did not load", bank.music_path[i]); dbg_print(msg); ok = false; continue; }
        t.bytes = h.loop_end / 2u; t.loop_byte = h.loop_start / 2u; t.rate = h.rate;
        t.loops = h.loop_start < h.samples;
        if (!t.loops) t.bytes = h.samples / 2u;
    }
    g_ntracks = bank.nmusic;
    if (!ok) return false;
#if !defined(NO_MUSIC)
    g_hnd = snd_stream_alloc(music_data, STREAM_BYTES);
    if (g_hnd == SND_STREAM_INVALID) { boot_log("audio: no stream"); return false; }
    snd_stream_queue_enable(g_hnd);
    kthread_t* th = thd_create(true, music_thread, nullptr);
    if (!th) { boot_log("audio: the music thread did not start"); return false; }
    thd_set_label(th, "music");
#endif
    g_stats.sound_ram_free = snd_mem_available();
    snprintf(msg, sizeof msg, "audio: AICA up, %d effects on %d channels, %d tracks%s, sound RAM free %lu KiB", g_nsfx, VOICES,
             g_ntracks,
#if defined(NO_MUSIC)
             " (NO_MUSIC build: no stream)",
#else
             " on 1 stream",
#endif
             (unsigned long)(g_stats.sound_ram_free / 1024));
    boot_log(msg);
    g_on = true;
    return true;
#endif
}

void snd_play(int voice, int sfx, int vol, int pan) {
    if (!g_on || voice < 0 || voice >= VOICES || sfx < 0 || sfx >= g_nsfx) return;
    snd_sfx_play_chn(g_chn[voice], g_sfx[sfx], vol, pan);
    ++g_stats.sfx_played;
}

void snd_music(int track, uint32_t start_sample, int vol, bool loop) {
    (void)start_sample;                                 // ADPCM tracks start at their beginning (docs/sound_save/STAGE_A_NOTES.md)
    if (!g_on) return;
    mutex_lock(&g_mx);
    g_req.gen = g_req.gen + 1; g_req.track = track; g_req.loop = loop; g_req.vol = vol;
    mutex_unlock(&g_mx);
}

void snd_music_volume(int vol) {
    if (!g_on) return;
    mutex_lock(&g_mx);
    g_req.vol = vol;
    mutex_unlock(&g_mx);
}

}  // namespace eng
