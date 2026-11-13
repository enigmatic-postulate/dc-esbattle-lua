// trace_main.cpp - drives the C++ battle with a recorded input script and writes
// a trace identical in format to harness/lua_ref.py.
//   usage: battle_trace <inputs.txt> <out.trace> [--bench N]
#include "../../src/battle.h"
#include "../../src/status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <chrono>

extern FILE*    g_trace;
extern uint32_t g_buttons;

static void snapshot() {
    bt::BattleCtx& ctx = bt::battle_ctx();
    fprintf(g_trace, "X %s active=%s turn=%d order=", bt::battle_state_name(ctx.state),
            ctx.active ? ctx.active->id : "nil", ctx.turn_i + 1);
    for (int i = 0; i < ctx.order.size(); ++i) fprintf(g_trace, "%s%s", i ? "," : "", ctx.order[i]->id);
    for (const bt::Actor& a : ctx.actors) {
        fprintf(g_trace, " %s:%d[", a.id, a.hp);
        for (int i = 0; i < a.status.size(); ++i)
            fprintf(g_trace, "%s%s:%d", i ? "," : "", bt::status_def(a.status[i].id).key, a.status[i].turns);
        fprintf(g_trace, "]");
    }
    fprintf(g_trace, "\n");
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s inputs out [--bench N]\n", argv[0]); return 2; }
    int bench = (argc >= 5 && !strcmp(argv[3], "--bench")) ? atoi(argv[4]) : 0;

    std::vector<std::pair<float, unsigned>> frames;
    FILE* in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }
    char line[128];
    while (fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        float dt; unsigned b;
        if (sscanf(line, "%f %x", &dt, &b) == 2) frames.push_back({ dt, b });
    }
    fclose(in);

    g_trace = fopen(bench ? "/dev/null" : argv[2], "w");
    if (!g_trace) { perror(argv[2]); return 1; }

    fprintf(g_trace, "F boot\n");
    bt::battle_init();

    auto t0 = std::chrono::steady_clock::now();
    int reps = bench ? bench : 1;
    for (int r = 0; r < reps; ++r) {
        if (r) { g_buttons = 0; bt::battle_start(); }
        int n = 0;
        for (auto& f : frames) {
            g_buttons = f.second;
            fprintf(g_trace, "F %d %.9g\n", ++n, (double)f.first);
            bt::battle_update((bt::real)f.first);
            bt::battle_draw();
            snapshot();
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    if (bench) {
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        printf("cpp: %d frames x %d reps: %.3f us/frame (incl. trace formatting)\n",
               (int)frames.size(), reps, us / (frames.size() * (double)reps));
    }
    fclose(g_trace);
    return 0;
}
