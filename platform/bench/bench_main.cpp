// bench_main.cpp - per-frame CPU cost of the C++ port with a no-op engine (host, indicative only).
#include "../../src/battle.h"
#include "../../src/engine.h"
#include <stdio.h>
#include <vector>
#include <chrono>

static uint32_t g_btn = 0, g_rng = 12345;
volatile uint32_t g_calls = 0;
namespace eng {
Sprite sprite_load(const char*) { static int n = 0; return ++n; }
void sprite_draw(Sprite, float, float, float, float, float, int) { g_calls = g_calls + 1; }
void sprite_draw_uv(Sprite, float, float, float, float, float, float, float, float, float, int) { g_calls = g_calls + 1; }
void gfx_rect(float, float, float, float, uint32_t, float) { g_calls = g_calls + 1; }
void gfx_rect_tr(float, float, float, float, uint32_t, float) { g_calls = g_calls + 1; }
bool input_down(Key k) { return (g_btn >> k) & 1; }
float random01() { g_rng = g_rng * 1664525u + 1013904223u; return (float)(g_rng >> 8) / 16777216.0f; }
void dbg_print(const char*) {}
}
int main(int argc, char** argv) {
    std::vector<std::pair<float, unsigned>> fr;
    FILE* in = fopen(argv[1], "r"); char line[128];
    while (fgets(line, sizeof line, in)) { float d; unsigned b; if (line[0] != '#' && sscanf(line, "%f %x", &d, &b) == 2) fr.push_back({d, b}); }
    fclose(in);
    bt::battle_init();
    int reps = argc > 2 ? atoi(argv[2]) : 50;
    double best = 1e9;
    for (int r = 0; r < reps; ++r) {
        bt::battle_start();
        auto t0 = std::chrono::steady_clock::now();
        int restarts = 0;
        for (auto& f : fr) {
            g_btn = f.second; bt::battle_update((bt::real)f.first); bt::battle_draw();
            bt::BattleState st = bt::battle_ctx().state;
            if (st == bt::BattleState::Win || st == bt::BattleState::Lose) { bt::battle_start(); ++restarts; }
        }
        if (r == 0) printf("cpp: battles completed: %d, engine calls %.1f/frame\n", restarts, (double)g_calls / fr.size());
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / fr.size();
        if (us < best) best = us;
    }
    printf("cpp: %.3f us/frame host CPU (best of %d), heap allocations in loop: 0 (fixed-capacity containers)\n", best, reps);
}
