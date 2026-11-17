// portrait.cpp - see portrait.h
#include "portrait.h"
#include "script.h"
#include <stdio.h>
#include <string.h>

namespace bt {

static const char* const VARIANT_FILE[VAR_COUNT] = { "neutral", "happy", "worried" };

struct Slot {
    eng::Sprite tex;
    int  spk, var;            // in VRAM (-1: blank)
    int  want_spk, want_var;  // requested
    int  hidden_frames;       // consecutive frames not drawn
    bool drawn_this_frame;
};
static Slot g_slot[PORTRAIT_SLOTS];
static bool g_ok = false;
static int  g_uploads = 0;
alignas(32) static uint16_t g_blank[PORTRAIT_SIZE * PORTRAIT_SIZE];
alignas(32) static char g_file[32 + PORTRAIT_SIZE * PORTRAIT_SIZE * 2 + 4];   // one .dtx being read (load time)
// the cache: texels of every speaker x variant, filled by portrait_preload() (static: no heap, no I/O in play)
alignas(32) static uint16_t g_cache[PORTRAIT_CACHE][PORTRAIT_SIZE * PORTRAIT_SIZE];
static int8_t g_cache_of[SCR_MAX_SPEAKERS][VAR_COUNT];      // cache index, -1 = not available
static int g_cached = 0;

bool portrait_init() {
    if (g_ok) return true;
    for (int i = 0; i < PORTRAIT_SLOTS; ++i) {
        g_slot[i].tex = eng::texture_create(g_blank, PORTRAIT_SIZE, PORTRAIT_SIZE, 1);
        if (!g_slot[i].tex) { eng::dbg_print("portrait: no texture slot left"); return false; }
    }
    portrait_reset();
    g_ok = true;
    return true;
}

void portrait_reset() {
    for (Slot& s : g_slot) { s.spk = s.var = s.want_spk = s.want_var = -1; s.hidden_frames = PORTRAIT_HIDE_FRAMES; s.drawn_this_frame = false; }
}

int portrait_preload() {
    memset(g_cache_of, -1, sizeof g_cache_of);
    g_cached = 0;
    for (int sp = 0; sp < script_speaker_count() && sp < SCR_MAX_SPEAKERS; ++sp)
        for (int v = 0; v < VAR_COUNT; ++v) {
            char path[64];
            snprintf(path, sizeof path, "/rd/portraits/%s_%s.dtx", script_speaker(sp).portrait, VARIANT_FILE[v]);
            const int n = eng::file_read(path, g_file, sizeof g_file);
            uint16_t h[4] = { 0, 0, 0, 0 };
            if (n == 32 + PORTRAIT_SIZE * PORTRAIT_SIZE * 2) memcpy(h, g_file + 4, sizeof h);
            if (n != 32 + PORTRAIT_SIZE * PORTRAIT_SIZE * 2 || memcmp(g_file, "DTX1", 4) || h[0] != PORTRAIT_SIZE || h[1] != PORTRAIT_SIZE || h[2] != 1) {
                char m[160];
                snprintf(m, sizeof m, "portrait: %s missing or not a 128x128 RGB565 .dtx", path);
                eng::dbg_print(m);
                continue;
            }
            if (g_cached >= PORTRAIT_CACHE) { eng::dbg_print("portrait: cache full (raise PORTRAIT_CACHE)"); continue; }
            const uint16_t* lin = (const uint16_t*)(g_file + 32);
#if defined(PORTRAIT_KOS_TWIDDLE)
            memcpy(g_cache[g_cached], lin, sizeof g_cache[0]);          // fallback: the KOS upload twiddles
#else
            // twiddled once here (load time), so an upload in play is a plain copy (no SH4 work per texel)
            for (int y = 0; y < PORTRAIT_SIZE; ++y)
                for (int x = 0; x < PORTRAIT_SIZE; ++x) g_cache[g_cached][eng::twiddle_index((uint32_t)x, (uint32_t)y)] = lin[y * PORTRAIT_SIZE + x];
#endif
            g_cache_of[sp][v] = (int8_t)g_cached++;
        }
    return g_cached;
}

static bool upload(int slot) {
    Slot& s = g_slot[slot];
    if (s.want_spk < 0 || s.want_spk >= script_speaker_count() || s.want_spk >= SCR_MAX_SPEAKERS || s.want_var < 0 ||
        s.want_var >= VAR_COUNT || g_cache_of[s.want_spk][s.want_var] < 0) {
        s.want_spk = s.spk; s.want_var = s.var;          // not available (logged at load): keep what is there
        return false;
    }
#if defined(PORTRAIT_KOS_TWIDDLE)
    const bool twiddled = false;
#else
    const bool twiddled = true;
#endif
    if (!eng::texture_update(s.tex, g_cache[g_cache_of[s.want_spk][s.want_var]], twiddled)) return false;
    s.spk = s.want_spk; s.var = s.want_var;
    ++g_uploads;
    return true;
}

void portrait_request(int slot, int speaker, int variant) {
    if (!g_ok || slot < 0 || slot >= PORTRAIT_SLOTS) return;
    Slot& s = g_slot[slot];
    s.want_spk = speaker; s.want_var = variant;
    if (s.spk == speaker && s.var == variant) return;
    if (s.hidden_frames >= PORTRAIT_HIDE_FRAMES) upload(slot);   // off screen: at once
}

void portrait_tick() {
    if (!g_ok) return;
    for (int i = 0; i < PORTRAIT_SLOTS; ++i) {
        Slot& s = g_slot[i];
        if ((s.spk != s.want_spk || s.var != s.want_var) && s.want_spk >= 0 && s.hidden_frames >= PORTRAIT_HIDE_FRAMES) upload(i);
    }
}

bool portrait_pending(int slot) {
    const Slot& s = g_slot[slot];
    return g_ok && s.want_spk >= 0 && (s.spk != s.want_spk || s.var != s.want_var);
}
bool portrait_ready(int slot) { return g_ok && g_slot[slot].spk >= 0 && !portrait_pending(slot); }
void portrait_drawn(int slot) { if (slot >= 0 && slot < PORTRAIT_SLOTS) g_slot[slot].drawn_this_frame = true; }
void portrait_frame_end() {
    for (Slot& s : g_slot) {
        s.hidden_frames = s.drawn_this_frame ? 0 : (s.hidden_frames < 1000 ? s.hidden_frames + 1 : 1000);
        s.drawn_this_frame = false;
    }
}
eng::Sprite portrait_sprite(int slot) { return g_slot[slot].tex; }
int portrait_uploads() { return g_uploads; }
int portrait_loaded(int slot, int* variant) { if (variant) *variant = g_slot[slot].var; return g_slot[slot].spk; }

}
