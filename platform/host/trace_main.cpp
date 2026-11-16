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
#include "../../src/battle3d.h"
#include "../../src/field.h"
#include "../../src/field3d.h"
#include "../../src/render3d.h"
#include "../../src/game.h"
#include "../../src/status.h"
#include "../../src/skills.h"
#include <math.h>
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
int  host_drops();
// battle_bot.cpp (host harness): closed-loop UI bot + turn-level rules log
void     bot_init(const char* policy);
bool     bot_enabled();
uint32_t bot_buttons();
void     turnlog_begin(FILE* f);
void     turnlog_tick(FILE* f);
void     turnlog_end(FILE* f);
void     bot_set_wait(int ticks);
void     bot_set_linger(int ticks);
void     bot_set_hold_a(bool on);

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
    const char* bot = nullptr;          // --bot POLICY: buttons come from the bot, not the input file
    const char* turn_log = nullptr;     // --turn-log FILE: rules-level log (battle_bot.cpp)
    int max_ticks = 60000;              // --max-ticks N: bot runs stop here at the latest
    const char* record = nullptr;
    bool bot_battles = false;
    const char* set_stats = nullptr;    // --set: balance tuning in battle-test mode (GDD 12.6)
    float way[16][2]; int n_way = 0, way_i = 0;   // --walk-to: the field walker's waypoints           // --bot-battles POLICY: the input file drives the field (paused
                                        // during battles), the bot plays every battle
    int dump_every = 0;
    std::vector<int> dump_frames;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--battle-test") && i + 1 < argc) battle_test = argv[++i];
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) map = argv[++i];
        else if (!strcmp(argv[i], "--romdisk") && i + 1 < argc) g_romdisk = argv[++i];
        else if (!strcmp(argv[i], "--state-only")) g_trace_draws = false;
        else if (!strcmp(argv[i], "--view")) view = true;
        else if (!strcmp(argv[i], "--bot") && i + 1 < argc) bot = argv[++i];
        else if (!strcmp(argv[i], "--bot-battles") && i + 1 < argc) { bot = argv[++i]; bot_battles = true; }
        else if (!strcmp(argv[i], "--walk-to") && i + 1 < argc) {            // field waypoints "x,y;x,y" (tiles)
            for (char* t = strtok(argv[++i], ";"); t && n_way < 16; t = strtok(nullptr, ";"))
                if (sscanf(t, "%f,%f", &way[n_way][0], &way[n_way][1]) == 2) ++n_way;
        }
        else if (!strcmp(argv[i], "--rules") && i + 1 < argc) {      // legacy: every slice rule switch off
            const char* r = argv[++i];
            if (!strcmp(r, "legacy")) { bt::rules().guard_fix = false; bt::rules().potions = false; }
            else if (strcmp(r, "game")) { fprintf(stderr, "unknown --rules %s (game|legacy)\n", r); return 2; }
        }
        else if (!strcmp(argv[i], "--bot-wait") && i + 1 < argc) bot_set_wait(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--bot-linger") && i + 1 < argc) bot_set_linger(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--bot-hold-a")) bot_set_hold_a(true);
        else if (!strcmp(argv[i], "--record-inputs") && i + 1 < argc) record = argv[++i];   // replayable .in
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) set_stats = argv[++i];   // "warden.hp=40,minion_a.str=3"
        else if (!strcmp(argv[i], "--view2d")) bt::battle_set_view(bt::BattleView::Sprites2D);   // the sprite battle
        else if (!strcmp(argv[i], "--tune3d") && i + 1 < argc) {
            char name[64]; float v;
            if (sscanf(argv[++i], "%63[^=]=%f", name, &v) != 2 || !bt::battle3d_tune(name, v)) { fprintf(stderr, "bad --tune3d %s\n", argv[i]); return 2; }
        }
        else if (!strcmp(argv[i], "--turn-log") && i + 1 < argc) turn_log = argv[++i];
        else if (!strcmp(argv[i], "--max-ticks") && i + 1 < argc) max_ticks = atoi(argv[++i]);
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
    FILE* in = strcmp(argv[1], "-") ? fopen(argv[1], "r") : nullptr;   // "-": no input file (bot runs)
    if (!in && !bot) { perror(argv[1]); return 1; }
    char line[128];
    while (in && fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        float dt; unsigned b; int ax = 0, ay = 0;
        int got = sscanf(line, "%f %x %d %d", &dt, &b, &ax, &ay);
        if (got >= 2) frames.push_back(In{ b, got == 4 ? ax : 0, got == 4 ? ay : 0 });
    }
    if (in) fclose(in);
    if (bot) bot_init(bot);
    if (bot && !bot_battles) frames.assign((size_t)max_ticks, In{ 0, 0, 0 });
    FILE* tlog = turn_log ? fopen(turn_log, "w") : nullptr;
    FILE* rec = record ? fopen(record, "w") : nullptr;
    if (tlog) turnlog_begin(tlog);

    g_trace = fopen(argv[2], "w");
    if (!g_trace) { perror(argv[2]); return 1; }

    fprintf(g_trace, "F boot\n");
    if (battle_test) bt::game_init_battle_test(battle_test);
    else if (!bt::game_init(map)) fprintf(stderr, "map load failed (see D lines in the trace)\n");
    if (battle_test && set_stats) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s", set_stats);
        for (char* t = strtok(buf, ","); t; t = strtok(nullptr, ",")) {
            char id[32], field[16]; int v;
            if (sscanf(t, "%31[^.].%15[^=]=%d", id, field, &v) != 3) { fprintf(stderr, "bad --set %s\n", t); return 2; }
            bool found = false;
            for (bt::Actor& a : bt::battle_ctx().actors) {
                if (strcmp(a.id, id)) continue;
                found = true;
                if (!strcmp(field, "hp")) { a.hp_max = v; if (a.hp > 0) a.hp = v; }
                else if (!strcmp(field, "str")) a.stats.str = v;
                else if (!strcmp(field, "def")) a.stats.def = v;
                else if (!strcmp(field, "mdef")) a.stats.mdef = v;
                else if (!strcmp(field, "int")) a.stats.intel = v;
                else if (!strcmp(field, "agi")) a.stats.agi = v;
                else { fprintf(stderr, "bad --set field %s\n", field); return 2; }
            }
            if (!strcmp(id, "party") && !strcmp(field, "potions")) { bt::battle_ctx().potions = v; found = true; }
            if (!strcmp(id, "rend") && !strcmp(field, "bonus")) { bt::g_soul_rend_bonus = v; found = true; }
            if (!found) { fprintf(stderr, "--set: no actor %s\n", id); return 2; }
        }
    }

    int n = 0;
    size_t fi = 0;
    int guard_ticks = 0;
    while (fi < frames.size() && guard_ticks < max_ticks) {
        ++guard_ticks;
        const bool in_battle = bt::game_mode() == bt::GameMode::Battle;
        const In& f = frames[fi];
        if (!(bot_battles && in_battle)) ++fi;               // the field script waits while a battle runs
        g_buttons = (bot && (!bot_battles || in_battle)) ? bot_buttons() : f.b;
        g_axis[0] = f.ax; g_axis[1] = f.ay;
        if (n_way && bt::game_mode() == bt::GameMode::Field && way_i < n_way) {
            // steer the stick toward the next waypoint (feet-box centre, tiles)
            const bt::Hero& h = bt::field_state().hero;
            const float hx = (float)(h.x + (bt::HERO_BOX_X0 + bt::HERO_BOX_X1) / 2 * bt::SUBPX) / (float)(bt::TILE_PX * bt::SUBPX);
            const float hy = (float)(h.y + (bt::HERO_BOX_Y0 + bt::HERO_BOX_Y1) / 2 * bt::SUBPX) / (float)(bt::TILE_PX * bt::SUBPX);
            const float dx = way[way_i][0] - hx, dy = way[way_i][1] - hy, d = sqrtf(dx * dx + dy * dy);
            if (d < 0.25f) { ++way_i; g_axis[0] = g_axis[1] = 0; }
            else { g_axis[0] = (int)(dx / d * 127.0f); g_axis[1] = (int)(dy / d * 127.0f); }
            g_buttons = 0;
        }
        if (rec) fprintf(rec, "0.0166667 %02x\n", g_buttons);
        fprintf(g_trace, "F %d %.9g\n", ++n, 1.0 / 60.0);
        bool dump = g_dump_dir && ((dump_every > 0 && n % dump_every == 0) ||
                                   std::find(dump_frames.begin(), dump_frames.end(), n) != dump_frames.end());
        host_frame_begin(n, dump);
        bt::game_tick();
        bt::game_draw();
        host_frame_end();
        if (tlog && bt::game_mode() == bt::GameMode::Battle) turnlog_tick(tlog);
        if (bt::game_mode() == bt::GameMode::Battle) snapshot_battle(); else snapshot_game();
        bt::GameMode md = bt::game_mode();
        if (view && md == bt::GameMode::Battle && bt::battle_view() == bt::BattleView::Models3D) {
            const bt::Battle3dInfo bi = bt::battle3d_info();
            const r3d::Stats& st = r3d::stats();
            fprintf(g_trace, "B seq=%s t=%d queued=%d busy=%d parts=%d proj=%d cam=%.2f,%.2f,%.2f tgt=%.2f,%.2f,%.2f fov=%.1f models=%d culled=%d statics=%d culled=%d tris=%d xf=%d out=%d arena=%d batches=%d drops=%d chars=%d weapons=%d ground=%d props=%d fx=%d\n",
                    bt::battle3d_seq_name(bi.seq_kind), bi.seq_t, bi.queued, bi.busy ? 1 : 0, bi.particles, bi.projectiles,
                    bi.cam_eye[0], bi.cam_eye[1], bi.cam_eye[2], bi.cam_target[0], bi.cam_target[1], bi.cam_target[2], bi.cam_fov,
                    st.models, st.models_culled, st.statics, st.statics_culled,
                    st.tris, st.verts_xf, st.verts_out, host_arena_used(), host_batches(), host_drops(),
                    bi.cost_chars, bi.cost_weapons, bi.cost_ground, bi.cost_props, bi.cost_fx);
            const bt::BattleCtx& bc = bt::battle_ctx();
            fprintf(g_trace, "U state=%s home=%d target=%d lock=%d busy=%d bstate=%s xs=",
                    bc.ui.state == bt::UiState::Home ? "home" : bc.ui.state == bt::UiState::Target ? "target" : "all",
                    bc.ui.home_i, bc.ui.target_i, bc.ui.target_entry_lock, bi.busy ? 1 : 0, bt::battle_state_name(bc.state));
            for (int k = 0; k < bi.n_enemies; ++k) fprintf(g_trace, "%s%.0f", k ? "," : "", bi.enemy_sx[k]);
            fprintf(g_trace, "\n");
        }
        if (view && (md == bt::GameMode::Field || md == bt::GameMode::ToBattle)) {
            bt::Field3dInfo vi = bt::field3d_info();
            const r3d::Stats& st = r3d::stats();
            fprintf(g_trace, "V cam=%.3f,%.3f yaw=%.3f walk=%.2f models=%d culled=%d statics=%d culled=%d near=%d strips_culled=%d tris=%d xf=%d out=%d arena=%d batches=%d\n",
                    vi.cam_x, vi.cam_z, vi.hero_yaw, vi.walk_weight, st.models, st.models_culled, st.statics, st.statics_culled,
                    st.statics_near, st.strips_culled,
                    st.tris, st.verts_xf, st.verts_out, host_arena_used(), host_batches());
        }
        if (bot && !bot_battles && bt::battle_outcome() != bt::BattleOutcome::None) break;
    }
    if (tlog) { turnlog_end(tlog); fclose(tlog); }
    if (rec) fclose(rec);
    fclose(g_trace);
    return 0;
}
