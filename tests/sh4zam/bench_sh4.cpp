// bench_sh4.cpp - SH4 cost of the game's tick + draw, for an instruction-counting emulator run
// (tests/sh4zam/README.md). The engine is a no-op except for the 3D vertex arena, which behaves
// like the KOS backend's (platform/kos/main.cpp): batches are appended to one 16k-vertex,
// 32-byte-aligned arena per frame and a batch that does not fit is dropped.
//
//   bench_sh4 <inputs.in> [romdisk dir] [--battle ENCOUNTER]   (--battle: start in that battle, as the
//   host harness's --battle-test; record the inputs of a bot-played battle with trace_main --record-inputs)
//
// Each input line is one 60 Hz tick followed by one draw, like the host trace harness. The three
// bench_mark_* functions below are empty, never inlined, and called around every tick and draw;
// the counting plugin attributes the instructions executed between them.
#include "../../src/engine.h"
#include "../../src/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
volatile int g_bench_sink;
__attribute__((noinline)) void bench_mark_tick(void) { g_bench_sink = 1; }
__attribute__((noinline)) void bench_mark_draw(void) { g_bench_sink = 2; }
__attribute__((noinline)) void bench_mark_end(void) { g_bench_sink = 3; }
}

static const char* g_rd = "romdisk";
static unsigned g_btn = 0;
static int g_axis[2] = { 0, 0 };
static unsigned g_rng = 12345;

const int ARENA_VERTS = 20480;   // as the KOS backend (slice 3D-5)
alignas(32) static eng::Vert3 g_arena[ARENA_VERTS];
static int g_used = 0, g_open = -1;
static unsigned g_drawn = 0, g_dropped = 0;

namespace eng {
Sprite sprite_load(const char*, bool) { static int n = 0; return ++n; }
void sprite_draw(Sprite, float, float, float, float, float, int) { g_bench_sink = 4; }
void sprite_draw_uv(Sprite, float, float, float, float, float, float, float, float, float, int, uint32_t, float) { g_bench_sink = 4; }
void sprite_reserve(Sprite, int, int) {}
void gfx_rect(float, float, float, float, uint32_t, float) { g_bench_sink = 5; }
void gfx_rect_tr(float, float, float, float, uint32_t, float) { g_bench_sink = 5; }
bool input_down(Key k) { return (g_btn >> k) & 1u; }
int  input_axis(Axis a) { return (a == AXIS_X || a == AXIS_Y) ? g_axis[a] : 0; }
Sprite texture_create(const uint16_t*, int, int, int) { static int n = 100; return ++n; }
bool texture_update(Sprite, const uint16_t*, bool) { return true; }   // slice 3D-5 (portraits): a 32 KiB copy on KOS, not modelled
Material material_create(Sprite, int) { static int n = 0; return ++n; }
Vert3* vtx_begin(int max) {
    if (g_open >= 0 || max <= 0 || g_used + max > ARENA_VERTS) { ++g_dropped; return nullptr; }
    g_open = g_used;
    return g_arena + g_used;
}
int vtx_room() { return g_open >= 0 ? 0 : ARENA_VERTS - g_used; }
void vtx_end(Material, int used) {
    if (g_open < 0) return;
    if (used > 0) { g_used = g_open + used; g_drawn += (unsigned)used; }
    g_open = -1;
}
void set_clear_color(uint32_t) {}
bool texture_update_vq(Sprite, const void*, int) { return true; }   // slice 3D-6 (the barrow's ground): a 130 KiB copy
void set_fog(bool, uint32_t, float, float) {}                          // slice 3D-6: PVR registers
void audio_event(const char*) {}
float random01() { g_rng = g_rng * 1664525u + 1013904223u; return (float)(g_rng >> 8) / 16777216.0f; }
void dbg_print(const char*) {}
void boot_log(const char*) {}
static void rd_path(char* p, int cap, const char* path) { snprintf(p, cap, "%s/%s", g_rd, strncmp(path, "/rd/", 4) == 0 ? path + 4 : path); }
int file_read(const char* path, char* buf, int cap) {
    char p[512]; rd_path(p, sizeof p, path);
    FILE* f = fopen(p, "rb"); if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)cap, f); fclose(f);
    if (n >= cap) return -1;
    buf[n] = 0;
    return n;
}
bool file_exists(const char* path) {
    char p[512]; rd_path(p, sizeof p, path);
    FILE* f = fopen(p, "rb"); if (!f) return false; fclose(f); return true;
}
}

// "<dt> <buttons-hex> [<stick-x> <stick-y>]": integer parsing only (the dt column is ignored).
static bool parse_line(const char* s, unsigned* b, int* ax, int* ay) {
    while (*s && *s != ' ' && *s != '\t') ++s;             // skip dt
    char* e;
    unsigned long v = strtoul(s, &e, 16);
    if (e == s) return false;
    *b = (unsigned)v; *ax = 0; *ay = 0;
    s = e; long x = strtol(s, &e, 10);
    if (e != s) { s = e; long y = strtol(s, &e, 10); if (e != s) { *ax = (int)x; *ay = (int)y; } }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s inputs.in [romdisk]\n", argv[0]); return 2; }
    if (argc > 2) g_rd = argv[2];
    const char* battle = (argc > 4 && !strcmp(argv[3], "--battle")) ? argv[4] : nullptr;
    FILE* in = fopen(argv[1], "r");
    if (!in) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    if (battle) bt::game_init_battle_test(battle);
    else if (!bt::game_init("/rd/maps/village.txt")) fprintf(stderr, "map failed to load\n");
    char line[128];
    int n = 0;
    while (fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        unsigned b; int ax, ay;
        if (!parse_line(line, &b, &ax, &ay)) continue;
        g_btn = b; g_axis[0] = ax; g_axis[1] = ay;
        g_used = 0; g_open = -1;
        bench_mark_tick();
        bt::game_tick();
        bench_mark_draw();
        bt::game_draw();
        bench_mark_end();
        ++n;
    }
    fclose(in);
    printf("frames %d, 3D vertices drawn %u, batches dropped (arena full) %u\n", n, g_drawn, g_dropped);
    return 0;
}
