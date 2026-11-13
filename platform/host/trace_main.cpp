// trace_main.cpp - drives the game with a recorded input script and writes a trace.
//   usage: battle_trace <inputs.txt> <out.trace> [--battle-test ENCOUNTER] [--map /rd/maps/x.txt]
//                       [--romdisk DIR] [--state-only]
// One input line = one fixed 60 Hz tick + one draw (the dt column is ignored).
#include "../../src/battle.h"
#include "../../src/field.h"
#include "../../src/game.h"
#include "../../src/status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern FILE*       g_trace;
extern bool        g_trace_draws;
extern uint32_t    g_buttons;
extern const char* g_romdisk;

static void snapshot_battle() {
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

static void snapshot_game() {
    const bt::FieldState& f = bt::field_state();
    const bt::Hero& h = f.hero;
    const bt::PartyState& p = bt::game_party();
    // hero=<x>,<y> sprite cell top-left in 1/16 px; ents=<id>:<tile x>,<tile y>:<px>,<py>:<alive|gone>
    fprintf(g_trace, "X mode=%s fade=%d hero=%d,%d,%s,%s cam=%d,%d hp=%d/%d pending=%d ents=",
            bt::game_mode_name(), bt::game_fade_alpha(), h.x, h.y, bt::dir_name(h.dir),
            h.moving ? "mv" : "idle", f.cam_x, f.cam_y, p.hero_hp, p.hero_hp_max, bt::game_pending_entity());
    for (int i = 0; i < f.ents.size(); ++i) {
        const bt::Entity& e = f.ents[i];
        const bt::Walker& w = e.w;
        int px = w.tx * bt::TILE_PX + (w.nx - w.tx) * bt::TILE_PX * w.step / w.step_len;
        int py = w.ty * bt::TILE_PX + (w.ny - w.ty) * bt::TILE_PX * w.step / w.step_len;
        fprintf(g_trace, "%s%s:%d,%d:%d,%d:%s", i ? "," : "", e.id, w.tx, w.ty, px, py, e.alive ? "alive" : "gone");
    }
    fprintf(g_trace, "\n");
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s inputs out [--battle-test ENC] [--map PATH] [--romdisk DIR] [--state-only]\n", argv[0]); return 2; }
    const char* battle_test = nullptr;
    const char* map = "/rd/maps/test_field.txt";
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--battle-test") && i + 1 < argc) battle_test = argv[++i];
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) map = argv[++i];
        else if (!strcmp(argv[i], "--romdisk") && i + 1 < argc) g_romdisk = argv[++i];
        else if (!strcmp(argv[i], "--state-only")) g_trace_draws = false;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }

    std::vector<unsigned> frames;
    FILE* in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }
    char line[128];
    while (fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        float dt; unsigned b;
        if (sscanf(line, "%f %x", &dt, &b) == 2) frames.push_back(b);
    }
    fclose(in);

    g_trace = fopen(argv[2], "w");
    if (!g_trace) { perror(argv[2]); return 1; }

    fprintf(g_trace, "F boot\n");
    if (battle_test) bt::game_init_battle_test(battle_test);
    else if (!bt::game_init(map)) fprintf(stderr, "map load failed (see D lines in the trace)\n");

    int n = 0;
    for (unsigned b : frames) {
        g_buttons = b;
        fprintf(g_trace, "F %d %.9g\n", ++n, 1.0 / 60.0);
        bt::game_tick();
        bt::game_draw();
        if (bt::game_mode() == bt::GameMode::Battle) snapshot_battle(); else snapshot_game();
    }
    fclose(g_trace);
    return 0;
}
