// host_engine.cpp - engine.h implementation for the host harness. Records every engine
// call as a text trace (format documented in harness/compare.py / tools/render_trace.py).
#include "../../src/engine.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

FILE*       g_trace = nullptr;
bool        g_trace_draws = true;         // false: --state-only (only F and X lines)
uint32_t    g_buttons = 0;                // active-high bitmask set by trace_main
const char* g_romdisk = "romdisk";        // host directory that stands in for /rd
static uint32_t g_rng = 12345;            // shared LCG (see lua_ref.py)

static const char* g_paths[eng::MAX_SPRITES];
static int g_nsprites = 0;

namespace eng {

Sprite sprite_load(const char* path, bool alpha) {
    if (g_nsprites >= MAX_SPRITES) return 0;
    g_paths[g_nsprites++] = path;
    if (g_trace_draws) fprintf(g_trace, "L %s %d\n", path, alpha ? 1 : 0);
    return g_nsprites;
}

void sprite_draw_uv(Sprite s, float x, float y, float w, float h, float a,
                    float u0, float v0, float u1, float v1, int filter, uint32_t argb, float z) {
    if (s < 1 || s > g_nsprites || !g_trace_draws) return;
    fprintf(g_trace, "S %s %.9g %.9g %.9g %.9g %.9g %d %.9g %.9g %.9g %.9g %08X %.9g\n", g_paths[s - 1],
            x, y, w, h, a, filter == 2 ? 2 : 0, u0, v0, u1, v1, (unsigned)argb, z);
}

void sprite_draw(Sprite s, float x, float y, float w, float h, float a, int filter) {
    sprite_draw_uv(s, x, y, w, h, a, 0, 0, 1, 1, filter);
}

void gfx_rect(float x, float y, float w, float h, uint32_t argb, float z) {
    if (g_trace_draws) fprintf(g_trace, "R OP %.9g %.9g %.9g %.9g %08X %.9g\n", x, y, w, h, (unsigned)argb, z);
}

void gfx_rect_tr(float x, float y, float w, float h, uint32_t argb, float z) {
    if (g_trace_draws) fprintf(g_trace, "R TR %.9g %.9g %.9g %.9g %08X %.9g\n", x, y, w, h, (unsigned)argb, z);
}

bool input_down(Key k) { return (g_buttons >> k) & 1u; }   // bit order == Key enum

float random01() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / 16777216.0f;   // 24-bit: exact in float and double
}

void dbg_print(const char* s) { fprintf(g_trace, "D %s\n", s); }

int file_read(const char* path, char* buf, int cap) {
    char real_path[512];
    if (strncmp(path, "/rd/", 4) == 0) snprintf(real_path, sizeof real_path, "%s/%s", g_romdisk, path + 4);
    else snprintf(real_path, sizeof real_path, "%s", path);
    FILE* f = fopen(real_path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    if (n >= cap) return -1;
    buf[n] = '\0';
    return n;
}

} // namespace eng
