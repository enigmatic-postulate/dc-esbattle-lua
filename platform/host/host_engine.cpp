// host_engine.cpp - engine.h implementation for the host equivalence harness.
// Records every engine call in the same text format as harness/lua_ref.py.
#include "../../src/engine.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

FILE*    g_trace = nullptr;
uint32_t g_buttons = 0;            // active-high bitmask set by trace_main
static uint32_t g_rng = 12345;     // shared LCG (see lua_ref.py)

static const char* g_paths[16];
static int g_nsprites = 0;

namespace eng {

Sprite sprite_load(const char* path) {
    if (g_nsprites >= 16) return 0;
    g_paths[g_nsprites++] = path;
    fprintf(g_trace, "L %s\n", path);
    return g_nsprites;
}

void sprite_draw(Sprite s, float x, float y, float w, float h, float a, int filter) {
    if (s < 1 || s > g_nsprites) return;
    fprintf(g_trace, "S %s %.9g %.9g %.9g %.9g %.9g %d\n", g_paths[s - 1], x, y, w, h, a, filter == 2 ? 2 : 0);
}

void gfx_rect(float x, float y, float w, float h, uint32_t argb, float z) {
    fprintf(g_trace, "R OP %.9g %.9g %.9g %.9g %08X %.9g\n", x, y, w, h, (unsigned)argb, z);
}

void gfx_rect_tr(float x, float y, float w, float h, uint32_t argb, float z) {
    fprintf(g_trace, "R TR %.9g %.9g %.9g %.9g %08X %.9g\n", x, y, w, h, (unsigned)argb, z);
}

bool input_down(Key k) {
    static const uint32_t BIT[KEY_COUNT] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    return (g_buttons & BIT[k]) != 0;
}

float random01() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / 16777216.0f;   // 24-bit: exact in float and double
}

void dbg_print(const char* s) { fprintf(g_trace, "D %s\n", s); }

} // namespace eng
