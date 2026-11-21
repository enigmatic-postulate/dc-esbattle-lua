// audio_unit.cpp - host unit checks of the audio logic and the ADPCM decoder (slice 3D-7; tests/sound_save/run.sh).
// Links src/audio.cpp alone, with a recording backend in place of the engine.
//   audio_unit --check FILE      the loader's error for a cue table ("" if fine)          (parity with lint_cues.py)
//   audio_unit --decode FILE...  per .adp: the header and the CRC of its decoded samples  (parity with adpcm.py)
//   audio_unit --logic ROMDISK   the queue, the voices, the music machine; prints ok/FAIL lines and a RESULT line
#include "../../src/audio.h"
#include "../../src/adpcm.h"
#include "../../src/engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static const char* g_romdisk = "romdisk";
struct Cmd { char kind; int a, b, c, d; };          // p voice sfx vol pan | m track start vol loop | v vol
static std::vector<Cmd> g_cmds;
static std::vector<std::string> g_log;

namespace eng {
static FILE* open_rd(const char* path) {
    char real[512];
    if (strncmp(path, "/rd/", 4) == 0) snprintf(real, sizeof real, "%s/%s", g_romdisk, path + 4);
    else snprintf(real, sizeof real, "%s", path);
    return fopen(real, "rb");
}
int file_read(const char* path, char* buf, int cap) {
    FILE* f = open_rd(path); if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)cap, f); fclose(f);
    if (n >= cap) return -1;
    buf[n] = 0; return n;
}
int file_read_at(const char* path, int offset, void* buf, int n) {
    FILE* f = open_rd(path); if (!f) return -1;
    int got = fseek(f, offset, SEEK_SET) == 0 ? (int)fread(buf, 1, (size_t)n, f) : -1;
    fclose(f); return got;
}
void audio_event(const char* name) { bt::audio_event(name); }
void dbg_print(const char* s) { g_log.push_back(s); }
void boot_log(const char* s) { g_log.push_back(s); }
bool snd_boot(const SndBank&) { return true; }
void snd_play(int voice, int sfx, int vol, int pan) { g_cmds.push_back(Cmd{ 'p', voice, sfx, vol, pan }); }
void snd_music(int track, uint32_t start, int vol, bool loop) { g_cmds.push_back(Cmd{ 'm', track, (int)start, vol, loop }); }
void snd_music_volume(int vol) { g_cmds.push_back(Cmd{ 'v', vol, 0, 0, 0 }); }
}

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}

static int decode_files(int n, char** files) {
    for (int k = 0; k < n; ++k) {
        FILE* f = fopen(files[k], "rb");
        if (!f) { printf("%s MISSING\n", files[k]); continue; }
        std::vector<uint8_t> blob;
        uint8_t tmp[65536]; size_t got;
        while ((got = fread(tmp, 1, sizeof tmp, f)) > 0) blob.insert(blob.end(), tmp, tmp + got);
        fclose(f);
        adpcm::Header h;
        if (blob.size() < 32 || !adpcm::parse_header(blob.data(), &h) || blob.size() - 32 < adpcm::data_bytes(h) ||
            crc32(blob.data() + 32, blob.size() - 32) != h.crc) { printf("%s BAD\n", files[k]); continue; }
        std::vector<int16_t> pcm(h.samples);
        adpcm::State s = adpcm::RESET, at_start = adpcm::RESET;
        const uint8_t* d = blob.data() + 32;
        for (uint32_t i = 0; i < h.samples; ++i) {
            if (i == h.loop_start) at_start = s;
            pcm[i] = h.format == adpcm::FMT_PCM8 ? (int16_t)((int8_t)d[i] * 256) : (int16_t)adpcm::decode(s, adpcm::code_at(d, i));
        }
        const bool seam = at_start.x == 0 && at_start.step == 127 && s.x == 0 && s.step == 127;
        printf("%s rate=%u samples=%u loop=%u..%u fmt=%u pcm_crc=%08x seam=%s\n", files[k], h.rate, h.samples, h.loop_start,
               h.loop_end, h.format, crc32((const uint8_t*)pcm.data(), pcm.size() * 2),
               h.loop_start == h.samples ? "none" : (seam || h.format == adpcm::FMT_PCM8 ? "ok" : "MISMATCH"));
    }
    return 0;
}

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what) { printf("  %s %s\n", ok ? "ok  " : "FAIL", what); (ok ? g_pass : g_fail)++; }
static int count(char kind) { int n = 0; for (const Cmd& c : g_cmds) n += c.kind == kind; return n; }
static int sfx_id(const char* name) { for (int i = 0; i < 64; ++i) if (strcmp(bt::audio_sfx_name(i), name) == 0) return i; return -1; }
static void frame(int ticks) { g_cmds.clear(); for (int i = 0; i < ticks; ++i) bt::audio_tick(); }
static void run(int frames) { for (int i = 0; i < frames; ++i) { bt::audio_tick(); bt::audio_frame(); } }

static int logic() {
    using namespace bt;
    check(audio_boot() && audio_ready(), "boot: the cue table and every header load");
    for (const std::string& l : g_log) printf("    log: %s\n", l.c_str());
    const AudioStats st0 = audio_stats();
    check(st0.cues_sfx == 20 && st0.cues_music == 8, "boot: the unit table's 20 effects and 8 tracks");

    // AU-T8: a 4-tick catch-up frame with a burst of events
    g_cmds.clear();
    for (int t = 0; t < 4; ++t) {
        audio_tick();
        for (int k = 0; k < 6; ++k) eng::audio_event("text_blip");
        static const char* const B[] = { "swing", "hit_bone", "fire_cast", "fire_impact", "menu_move", "menu_confirm", "talk",
                                         "potion", "guard_up", "soul_rend", "charge", "gate" };
        for (const char* n : B) { bt::audio_event(n); bt::audio_event(n); }
    }
    bt::audio_music("village", 0, 0);
    audio_frame();
    int blips = 0, most = 0; int per[64] = { 0 };
    for (const Cmd& c : g_cmds) if (c.kind == 'p') { blips += c.b == sfx_id("text_blip"); if (++per[c.b] > most) most = per[c.b]; }
    check((int)g_cmds.size() <= AUDIO_MAX_CMDS, "burst: at most 8 backend commands in the frame");
    check(most <= 1, "burst: no cue twice in one frame");
    check(blips <= 1, "burst: one text blip at most");
    bool has_top = false; for (const Cmd& c : g_cmds) has_top |= c.kind == 'p' && c.b == sfx_id("soul_rend");
    check(has_top && count('m') == 1, "burst: the music change and the highest-priority effect made it");
    check(audio_stats().dropped_cmds > st0.dropped_cmds && audio_stats().dropped_dupe > st0.dropped_dupe, "burst: the drops are counted");

    // rate limit: text_blip (min_gap 3) fired every tick for 30 ticks plays 10 times
    run(120);
    g_cmds.clear(); blips = 0;
    for (int t = 0; t < 30; ++t) { audio_tick(); bt::audio_event("text_blip"); audio_frame(); }
    for (const Cmd& c : g_cmds) blips += c.kind == 'p' && c.b == sfx_id("text_blip");
    check(blips == 10, "rate limit: a blip every tick for 30 ticks plays 10 times (one per 3 ticks)");

    // voices: 8 long low-priority sounds, then a higher one takes the oldest; an equal one is dropped
    run(400);
    static const char* const LONG8[] = { "warden_rise", "cheer", "gate", "charge", "summon", "waystone", "boss_name", "phase_change" };
    for (const char* n : LONG8) { audio_tick(); bt::audio_event(n); audio_frame(); }
    g_cmds.clear();
    audio_tick(); bt::audio_event("soul_rend"); audio_frame();
    check(g_cmds.size() == 1 && g_cmds[0].kind == 'p' && g_cmds[0].a == 0,
          "voices: with 8 busy, the top-priority cue replaces the oldest lower-priority voice");
    const int dv = audio_stats().dropped_voices;
    g_cmds.clear();
    audio_tick(); bt::audio_event("text_blip"); audio_frame();
    check(g_cmds.empty() && audio_stats().dropped_voices == dv + 1, "voices: a low-priority cue finds no voice and is dropped");
    run(400);
    g_cmds.clear();
    audio_tick(); bt::audio_event("text_blip"); audio_frame();
    check(g_cmds.size() == 1 && g_cmds[0].a == 0, "voices: once the sounds have ended, voice 0 is free again");

    // music: fade out / in on ticks, the same track is not restarted, levels are monotonic
    audio_set_options(AudioOptions{ 10, 10, false });
    run(60);
    g_cmds.clear();
    bt::audio_music("barrow", 20, 20);
    std::vector<int> levels;
    int starts = 0, start_tick = -1;
    for (int t = 0; t < 60; ++t) {
        audio_tick(); audio_frame();
        for (const Cmd& c : g_cmds) { if (c.kind == 'm') { ++starts; start_tick = t; } }
        levels.push_back(audio_music_level());
        g_cmds.clear();
    }
    bool down = true, up = true;
    for (int t = 1; t < 20; ++t) down &= levels[t] <= levels[t - 1];
    for (int t = 21; t < 40; ++t) up &= levels[t] >= levels[t - 1];
    check(starts == 1 && start_tick == 19, "music: the new track starts when the 20-tick fade out ends");
    check(down && up && levels[45] == 170 && strcmp(audio_music_name(), "barrow") == 0, "music: level falls for 20 ticks, rises for 20, ends at the cue's 170");
    bt::audio_music("barrow", 20, 20); eng::audio_event("music_barrow");
    frame(1); audio_frame();
    check(g_cmds.empty(), "music: asking for the track that is playing changes nothing");

    // ducking and options
    audio_duck(60, 12); run(12);
    check(audio_music_level() == 170 * 60 / 100, "duck: 60 % after 12 ticks");
    audio_duck(100, 12); run(12);
    check(audio_music_level() == 170, "duck: back to full after 12");
    audio_set_options(AudioOptions{ 0, 10, false }); run(1);
    check(audio_music_level() == 0, "options: music 0 is silence");
    audio_set_options(AudioOptions{ 5, 5, true }); run(1);
    g_cmds.clear();
    audio_tick(); bt::audio_event("swing", 30); audio_frame();
    check(audio_music_level() == 170 * 25 / 100 && g_cmds.size() == 1 && g_cmds[0].c == 220 * 25 / 100 && g_cmds[0].d == 128,
          "options: level 5 is a quarter (squared curve); MONO centres a panned cue");
    audio_set_options(AudioOptions{ 8, 10, false });
    run(10); g_cmds.clear();
    audio_tick(); bt::audio_event("swing", 30); bt::audio_event("menu_move", 30); audio_frame();
    bool pans = g_cmds.size() == 2;
    for (const Cmd& c : g_cmds) pans &= c.d == (c.b == sfx_id("swing") ? 30 : 128);
    check(pans, "pan: a pan_actor cue takes the pan, others stay centred");

    // stings: at once, no loop, then silence
    g_cmds.clear();
    eng::audio_event("sting_victory"); audio_tick(); audio_frame();
    check(g_cmds.size() == 1 && g_cmds[0].kind == 'm' && g_cmds[0].d == 0 && strcmp(audio_music_name(), "victory") == 0, "sting: starts at once, not looped");
    g_cmds.clear(); run(300);
    check(count('m') == 1 && g_cmds.back().a == -1 && audio_music_name()[0] == 0, "sting: the stream stops when it has played");
    const int unk = audio_stats().unknown_events;
    eng::audio_event("no_such_event"); eng::audio_event("music_victory"); eng::audio_event("sting_village");
    check(audio_stats().unknown_events == unk + 3, "unknown events are counted, not played (a loop is not a sting and the reverse)");
    eng::audio_event("skip"); eng::audio_event("hit"); eng::audio_event("music_credits");
    check(audio_stats().unknown_events == unk + 3, "aliases resolve (skip, hit, music_credits)");
    printf("RESULT: pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && strcmp(argv[1], "--check") == 0) {
        static char text[16384];
        FILE* f = fopen(argv[2], "rb"); if (!f) { perror(argv[2]); return 2; }
        size_t n = fread(text, 1, sizeof text - 1, f); fclose(f); text[n] = 0;
        printf("%s\n", bt::audio_check_table(text));
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--decode") == 0) return decode_files(argc - 2, argv + 2);
    if (argc >= 3 && strcmp(argv[1], "--logic") == 0) { g_romdisk = argv[2]; return logic(); }
    fprintf(stderr, "usage: audio_unit --check FILE | --decode FILE... | --logic ROMDISK\n");
    return 2;
}
