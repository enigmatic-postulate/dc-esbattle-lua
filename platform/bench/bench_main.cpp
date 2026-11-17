// bench_main.cpp - per-tick CPU cost of the game with a no-op engine (host, indicative only).
//   usage: bench <inputs.txt> [reps] [--battle]      (default: field game from romdisk/)
#include "../../src/game.h"
#include "../../src/engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <chrono>

static uint32_t g_btn = 0, g_rng = 12345;
volatile uint32_t g_calls = 0;
namespace eng {
Sprite sprite_load(const char*, bool) { static int n = 0; return ++n; }
void sprite_draw(Sprite, float, float, float, float, float, int) { g_calls = g_calls + 1; }
void sprite_draw_uv(Sprite, float, float, float, float, float, float, float, float, float, int, uint32_t, float) { g_calls = g_calls + 1; }
void gfx_rect(float, float, float, float, uint32_t, float) { g_calls = g_calls + 1; }
void gfx_rect_tr(float, float, float, float, uint32_t, float) { g_calls = g_calls + 1; }
bool input_down(Key k) { return (g_btn >> k) & 1; }
int  input_axis(Axis) { return 0; }
Sprite texture_create(const uint16_t*, int, int, int) { static int n = 100; return ++n; }
Sprite texture_create_vq(const void*, int, int, int) { static int n = 200; return ++n; }
bool texture_update(Sprite, const uint16_t*, bool) { return true; }   // slice 3D-5 (portraits): a 32 KiB copy on KOS, not modelled
Material material_create(Sprite, int) { static int n = 0; return ++n; }
alignas(32) static Vert3 g_vtx[16384];
Vert3* vtx_begin(int max) { return max <= 16384 ? g_vtx : nullptr; }
void vtx_end(Material, int used) { g_calls = g_calls + (uint32_t)used; }
void set_clear_color(uint32_t) {}
bool file_exists(const char* path) {
    char p[512]; snprintf(p, sizeof p, "romdisk/%s", strncmp(path, "/rd/", 4) == 0 ? path + 4 : path);
    FILE* f = fopen(p, "rb"); if (!f) return false; fclose(f); return true;
}
float random01() { g_rng = g_rng * 1664525u + 1013904223u; return (float)(g_rng >> 8) / 16777216.0f; }
void dbg_print(const char* s) { fprintf(stderr, "%s\n", s); }
void boot_log(const char*) {}
int file_read(const char* path, char* buf, int cap) {
    char p[512]; snprintf(p, sizeof p, "romdisk/%s", strncmp(path, "/rd/", 4) == 0 ? path + 4 : path);
    FILE* f = fopen(p, "rb"); if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)cap, f); fclose(f);
    if (n >= cap) return -1; buf[n] = 0; return n;
}
}
int main(int argc, char** argv) {
    std::vector<unsigned> fr;
    FILE* in = fopen(argv[1], "r"); char line[128];
    while (fgets(line, sizeof line, in)) { float d; unsigned b; if (line[0] != '#' && sscanf(line, "%f %x", &d, &b) == 2) fr.push_back(b); }
    fclose(in);
    int reps = argc > 2 ? atoi(argv[2]) : 20;
    bool battle = argc > 3 && !strcmp(argv[3], "--battle");
    double best = 1e9;
    for (int r = 0; r < reps; ++r) {
        if (battle) bt::game_init_battle_test("test_slimes"); else bt::game_init("/rd/maps/village.txt");
        g_calls = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (unsigned b : fr) { g_btn = b; bt::game_tick(); bt::game_draw(); }
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / fr.size();
        if (us < best) best = us;
    }
    printf("%s: %.3f us/tick host CPU (best of %d), %.1f engine calls/tick\n", battle ? "battle" : "field", best, reps, (double)g_calls / fr.size());
}
