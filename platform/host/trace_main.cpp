// trace_main.cpp - drives the game with a recorded input script and writes a trace.
//   usage: battle_trace <inputs.txt> <out.trace> [--battle-test ENCOUNTER] [--map /rd/maps/x.txt]
//                       [--romdisk DIR] [--state-only] [--view]
//                       [--dump DIR --dump-frames 1,60,120 | --dump-every N]
// One input line = one fixed 60 Hz tick + one draw (the dt column is ignored).
// Input line: "<dt> <buttons-hex> [<stick-x> <stick-y>]"; the stick columns (-128..127) are
// optional, so the older button-only scripts still work.
// --view adds a "V" line per field tick with the 3D view's cosmetic state (camera, yaw, blend).
// --dump writes every draw (3D vertices included) of the selected ticks to DIR/frame_NNNNN.txt
// for tools/render_frame.py.
#include "../../src/battle.h"
#include "../../src/field.h"
#include "../../src/field3d.h"
#include "../../src/render3d.h"
#include "../../src/game.h"
#include "../../src/status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

extern FILE*       g_trace;
extern bool        g_trace_draws;
extern uint32_t    g_buttons;
extern int         g_axis[2];
extern const char* g_romdisk;
extern const char* g_dump_dir;
void host_frame_begin(int tick, bool dump);
void host_frame_end();
int  host_arena_used();
int  host_batches();

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
    const char* map = "/rd/maps/village.txt";
    bool view = false;
    int dump_every = 0;
    std::vector<int> dump_frames;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--battle-test") && i + 1 < argc) battle_test = argv[++i];
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) map = argv[++i];
        else if (!strcmp(argv[i], "--romdisk") && i + 1 < argc) g_romdisk = argv[++i];
        else if (!strcmp(argv[i], "--state-only")) g_trace_draws = false;
        else if (!strcmp(argv[i], "--view")) view = true;
        else if (!strcmp(argv[i], "--tune") && i + 1 < argc) {
            char name[64]; float v;
            if (sscanf(argv[++i], "%63[^=]=%f", name, &v) != 2 || !bt::field3d_tune(name, v)) {
                fprintf(stderr, "bad --tune %s\n", argv[i]); return 2;
            }
        }
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) g_dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) dump_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-frames") && i + 1 < argc) {
            for (char* t = strtok(argv[++i], ","); t; t = strtok(nullptr, ",")) dump_frames.push_back(atoi(t));
        }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }

    struct In { unsigned b; int ax, ay; };
    std::vector<In> frames;
    FILE* in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }
    char line[128];
    while (fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        float dt; unsigned b; int ax = 0, ay = 0;
        int got = sscanf(line, "%f %x %d %d", &dt, &b, &ax, &ay);
        if (got >= 2) frames.push_back(In{ b, got == 4 ? ax : 0, got == 4 ? ay : 0 });
    }
    fclose(in);

    g_trace = fopen(argv[2], "w");
    if (!g_trace) { perror(argv[2]); return 1; }

    fprintf(g_trace, "F boot\n");
    if (battle_test) bt::game_init_battle_test(battle_test);
    else if (!bt::game_init(map)) fprintf(stderr, "map load failed (see D lines in the trace)\n");

    int n = 0;
    for (const In& f : frames) {
        g_buttons = f.b;
        g_axis[0] = f.ax; g_axis[1] = f.ay;
        fprintf(g_trace, "F %d %.9g\n", ++n, 1.0 / 60.0);
        bool dump = g_dump_dir && ((dump_every > 0 && n % dump_every == 0) ||
                                   std::find(dump_frames.begin(), dump_frames.end(), n) != dump_frames.end());
        host_frame_begin(n, dump);
        bt::game_tick();
        bt::game_draw();
        host_frame_end();
        if (bt::game_mode() == bt::GameMode::Battle) snapshot_battle(); else snapshot_game();
        bt::GameMode md = bt::game_mode();
        if (view && (md == bt::GameMode::Field || md == bt::GameMode::ToBattle)) {
            bt::Field3dInfo vi = bt::field3d_info();
            const r3d::Stats& st = r3d::stats();
            fprintf(g_trace, "V cam=%.3f,%.3f yaw=%.3f walk=%.2f models=%d culled=%d statics=%d culled=%d near=%d strips_culled=%d tris=%d xf=%d out=%d arena=%d batches=%d\n",
                    vi.cam_x, vi.cam_z, vi.hero_yaw, vi.walk_weight, st.models, st.models_culled, st.statics, st.statics_culled,
                    st.statics_near, st.strips_culled,
                    st.tris, st.verts_xf, st.verts_out, host_arena_used(), host_batches());
        }
    }
    fclose(g_trace);
    return 0;
}
