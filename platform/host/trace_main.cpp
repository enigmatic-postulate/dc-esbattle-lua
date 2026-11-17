// trace_main.cpp - drives the game with a recorded input script and writes a trace.
//   usage: battle_trace <inputs.txt> <out.trace> [--battle-test ENCOUNTER] [--map /rd/maps/x.txt]
//                       [--romdisk DIR] [--state-only] [--view]
//                       [--dump DIR --dump-frames 1,60,120 | --dump-every N]
// One input line = one fixed 60 Hz tick + one draw (the dt column is ignored).
// Input line: "<dt> <buttons-hex> [<stick-x> <stick-y>]"; the stick columns (-128..127) are
// optional, so the older button-only scripts still work.
// --view adds a "V" line per field tick with the 3D view's cosmetic state (camera, yaw, blend).
// Slice 3D-5 (dialog, quest):
//   --route FILE          story driver (platform/host/story.cpp): talk / chase / goto / choose / expect steps;
//                         battles are played by the bot (--bot-battles POLICY, default smart). Implies --story.
//   --choices P           answers when the route queue is empty: first | last | cancel | k,k,...
//   --story               Q (Progress), G (dialog UI), N (mode) and E (route expectation) lines
//   --dump-scripts        prints the compiled scripts (== tools/dialog/lint.py --dump) and exits
//   --probe-talk          prompt geometry: P lines of the talk target over a grid of hero positions x facings
//   --probe-cam           the dialog two-shot from every walkable tile around each NPC (C lines)
//   --skills N            battle-test party skills (bit set; 0 = Fire not learned)
//   --resources           after the boot: D line with texture slots, VRAM, materials, model arena, library, script pools
//   --sweep               frame cost with the hero on every walkable tile (the boot's entities; the camera and the
//                         fades settled; after 90 ticks, past the boot fade-in): S <x> <y> out=<submitted> xf=<transformed>
//                         arena=<written> drops=<n> statics=<drawn> near=<crossing the near plane> models=<drawn> lines
//   --text-widths FILE    prints "W <px> <glyphs>" for each line of FILE with the dialog font (parity with lint.py)
// --dump writes every draw (3D vertices included) of the selected ticks to DIR/frame_NNNNN.txt
// for tools/render_frame.py.
#include "../../src/battle.h"
#include "../../src/battle3d.h"
#include "../../src/field.h"
#include "../../src/field3d.h"
#include "../../src/render3d.h"
#include "../../src/game.h"
#include "../../src/progress.h"
#include "../../src/script.h"
#include "../../src/text.h"
#include "../../src/anim.h"
#include "../../src/dmf.h"
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
extern int         g_alloc_watch;   // host_engine.cpp (-DHOST_ALLOC_COUNT): the per-tick allocation counter
extern long        g_alloc_count;
void host_frame_begin(int tick, bool dump);
void host_frame_end();
int  host_arena_used();
int  host_batches();
int  host_drops();
int  host_sprites();
int  host_rects();
int  host_materials();
int  host_textures();
long host_vram_bytes();
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
// story.cpp (host harness): the route driver
bool     story_load(const char* path);
void     story_set_choices(const char* policy);
uint32_t story_buttons(int* ax, int* ay);
bool     story_done();
int      story_failures();
int      story_expects();
void     story_trace(FILE* t, int tick);

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
        fprintf(g_trace, "%s%s:%d,%d:%d,%d:%s", i ? "," : "", e.id, w.tx, w.ty, px, py, !e.alive ? "gone" : e.hidden ? "hidden" : "alive");
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
    const char* route = nullptr;        // --route FILE (story driver)
    bool story = false, dump_scripts = false, probe_talk = false, probe_cam = false;
    int skills = -1;
    const char* text_widths = nullptr;
    bool resources = false, sweep = false;
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
            if (!strcmp(r, "legacy")) { bt::rules().guard_fix = false; bt::rules().potions = false; bt::rules().learned_skills = false; }
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
        else if (!strcmp(argv[i], "--route") && i + 1 < argc) { route = argv[++i]; story = true; }
        else if (!strcmp(argv[i], "--choices") && i + 1 < argc) story_set_choices(argv[++i]);
        else if (!strcmp(argv[i], "--story")) story = true;
        else if (!strcmp(argv[i], "--dump-scripts")) dump_scripts = true;
        else if (!strcmp(argv[i], "--probe-talk")) probe_talk = true;
        else if (!strcmp(argv[i], "--probe-cam")) probe_cam = true;
        else if (!strcmp(argv[i], "--skills") && i + 1 < argc) skills = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--text-widths") && i + 1 < argc) text_widths = argv[++i];
        else if (!strcmp(argv[i], "--resources")) resources = true;
        else if (!strcmp(argv[i], "--sweep")) sweep = true;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (route) {
        if (!story_load(route)) return 2;
        if (!bot) { bot = "smart"; }
        bot_battles = true;
    }

    struct In { unsigned b; int ax, ay; };
    std::vector<In> frames;
    FILE* in = strcmp(argv[1], "-") ? fopen(argv[1], "r") : nullptr;   // "-": no input file (bot runs)
    if (!in && !bot && !dump_scripts && !probe_talk && !probe_cam && !text_widths && !sweep) { perror(argv[1]); return 1; }
    char line[128];
    while (in && fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        float dt; unsigned b; int ax = 0, ay = 0;
        int got = sscanf(line, "%f %x %d %d", &dt, &b, &ax, &ay);
        if (got >= 2) frames.push_back(In{ b, got == 4 ? ax : 0, got == 4 ? ay : 0 });
    }
    if (in) fclose(in);
    if (bot) bot_init(bot);
    if ((bot && !bot_battles) || (route && frames.empty())) frames.assign((size_t)max_ticks, In{ 0, 0, 0 });
    FILE* tlog = turn_log ? fopen(turn_log, "w") : nullptr;
    FILE* rec = record ? fopen(record, "w") : nullptr;
    if (tlog) turnlog_begin(tlog);

    g_trace = fopen(argv[2], "w");
    if (!g_trace) { perror(argv[2]); return 1; }

    fprintf(g_trace, "F boot\n");
    if (battle_test) bt::game_init_battle_test(battle_test, skills);
    else if (!bt::game_init(map)) fprintf(stderr, "map load failed (see D lines in the trace)\n");
    if (resources) {
        const bt::ScriptStats st = bt::script_stats();
        fprintf(g_trace, "D resources: textures %d/32, VRAM %ld KiB, materials %d/256, model arena %d KiB, animation library %d KiB, script pools %d B (compiler scratch %d B)\n",
                host_textures(), host_vram_bytes() / 1024, host_materials(), dmf::loaded_bytes() / 1024,
                anim::anim_library_bytes() / 1024, st.pool_bytes, st.scratch_bytes);
    }
    if (text_widths) {
        FILE* tf = fopen(text_widths, "r");
        if (!tf) { perror(text_widths); return 1; }
        char tl[600];
        while (fgets(tl, sizeof tl, tf)) {
            size_t L = strlen(tl);
            while (L && (tl[L - 1] == '\n' || tl[L - 1] == '\r')) tl[--L] = 0;
            printf("W %d %d\n", bt::text_width(tl), bt::text_glyph_count(tl));
        }
        fclose(tf);
        fclose(g_trace);
        return 0;
    }
    if (dump_scripts) {
        bt::script_dump([](const char* text, void*) { printf("%s\n", text); }, nullptr);
        fclose(g_trace);
        return bt::script_loaded() ? 0 : 1;
    }
    if (probe_talk) {
        // P <npc index or -1> <hero x> <hero y> <face x> <face y>: the hero's sprite cell (1/16 px) on a 1/8-tile grid
        // within 2 tiles of each NPC's home, 16 facings each (tests/dialog/check_talk.py models GDD 7.2 / plan 5.1)
        const bt::FieldState& f = bt::field_state();
        static const int FX[16] = { 256, 237, 181, 98, 0, -98, -181, -237, -256, -237, -181, -98, 0, 98, 181, 237 };
        static const int FY[16] = { 0, 98, 181, 237, 256, 237, 181, 98, 0, -98, -181, -237, -256, -237, -181, -98 };
        for (int n = 0; n < f.ents.size(); ++n) {
            if (f.ents[n].kind != bt::EntityKind::Npc) continue;
            const int T = bt::TILE_PX * bt::SUBPX;
            for (int gy = -16; gy <= 16; ++gy)
                for (int gx = -16; gx <= 16; ++gx) {
                    // feet-box centre at the NPC's tile centre + (gx, gy) eighths of a tile
                    const int fx = f.ents[n].home_x * T + T / 2 + gx * T / 8, fy = f.ents[n].home_y * T + T / 2 + gy * T / 8;
                    const int hx = fx - (bt::HERO_BOX_X0 + bt::HERO_BOX_X1) / 2 * bt::SUBPX, hy = fy - (bt::HERO_BOX_Y0 + bt::HERO_BOX_Y1) / 2 * bt::SUBPX;
                    for (int k = 0; k < 16; ++k) {
                        bt::field_debug_place_hero(hx, hy, FX[k] * 3, FY[k] * 3);
                        printf("P %d %d %d %d %d\n", bt::field_debug_talk_target(), hx, hy, FX[k] * 3, FY[k] * 3);
                    }
                }
        }
        fclose(g_trace);
        return 0;
    }
    if (probe_cam) {
        // C <npc> <tile x> <tile y> <ok> <eye x y z>: the two-shot from every walkable tile within 2 tiles of each NPC spot
        const bt::FieldState& f = bt::field_state();
        for (int n = 0; n < f.ents.size(); ++n) {
            const bt::Entity& e = f.ents[n];
            if (e.kind != bt::EntityKind::Npc) continue;
            const int x0 = e.home_x - 2, x1 = e.home_x + 2 + (e.patrol ? e.plen * (e.pdx > 0 ? e.pdx : 0) : 0);
            const int y0 = e.home_y - 2, y1 = e.home_y + 2 + (e.patrol ? e.plen * (e.pdy > 0 ? e.pdy : 0) : 0);
            for (int ty = y0; ty <= y1; ++ty)
                for (int tx = x0; tx <= x1; ++tx) {
                    if (!bt::cell_walkable(tx, ty)) continue;
                    for (int p = 0; p <= (e.patrol ? e.plen : 0); ++p) {
                        const float nx = e.home_x + 0.5f + p * e.pdx, nz = e.home_y + 0.625f + p * e.pdy;
                        if (tx == (int)nx && ty == (int)nz) continue;
                        float eye[3], look[3];
                        const bool ok = bt::field3d_two_shot(tx + 0.5f, ty + 0.5f, nx, nz, eye, look);
                        printf("C %s %d %d %d %.2f %.2f %.2f npc_at=%.1f,%.1f\n", e.id, tx, ty, ok ? 1 : 0, eye[0], eye[1], eye[2], nx, nz);
                    }
                }
        }
        fclose(g_trace);
        return 0;
    }
    if (sweep) {
        // the hero's feet-box centre on each walkable tile's centre, facing south; field3d_reset puts the camera
        // where it settles (no lead), 30 view ticks let the fades finish; then one frame is drawn and measured
        for (int t = 0; t < 90; ++t) bt::game_tick();        // past the boot fade-in (the entities start their patrols)
        const bt::FieldState& f = bt::field_state();
        const int T = bt::TILE_PX * bt::SUBPX;
        int k = 0;
        for (int ty = 0; ty < f.h; ++ty)
            for (int tx = 0; tx < f.w; ++tx) {
                if (!bt::cell_walkable(tx, ty)) continue;
                const int hx = tx * T + T / 2 - (bt::HERO_BOX_X0 + bt::HERO_BOX_X1) / 2 * bt::SUBPX;
                const int hy = ty * T + T / 2 - (bt::HERO_BOX_Y0 + bt::HERO_BOX_Y1) / 2 * bt::SUBPX;
                bt::field_debug_place_hero(hx, hy, 0, 256);
                bt::field3d_reset();
                for (int t = 0; t < 30; ++t) bt::field3d_tick();
                const bool dump = g_dump_dir && std::find(dump_frames.begin(), dump_frames.end(), ++k) != dump_frames.end();
                host_frame_begin(k, dump);
                bt::game_draw();
                const r3d::Stats& st = r3d::stats();
                printf("S %d %d out=%d xf=%d arena=%d drops=%d statics=%d near=%d models=%d\n", tx, ty, st.verts_out, st.verts_xf,
                       host_arena_used(), host_drops(), st.statics - st.statics_culled, st.statics_near, st.models - st.models_culled);
                host_frame_end();
            }
        fclose(g_trace);
        return 0;
    }
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
        if (route && !in_battle) { int ax, ay; g_buttons = story_buttons(&ax, &ay); g_axis[0] = ax; g_axis[1] = ay; }
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
        if (rec) {
            if (g_axis[0] || g_axis[1]) fprintf(rec, "0.0166667 %02x %d %d\n", g_buttons, g_axis[0], g_axis[1]);
            else fprintf(rec, "0.0166667 %02x\n", g_buttons);
        }
        fprintf(g_trace, "F %d %.9g\n", ++n, 1.0 / 60.0);
        bool dump = g_dump_dir && ((dump_every > 0 && n % dump_every == 0) ||
                                   std::find(dump_frames.begin(), dump_frames.end(), n) != dump_frames.end());
        host_frame_begin(n, dump);
        g_alloc_watch = n > 1;                       // after the boot: no allocation may happen in a tick or a draw
        bt::game_tick();
        bt::game_draw();
        g_alloc_watch = 0;
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
        if (view && (md == bt::GameMode::Field || md == bt::GameMode::ToBattle || md == bt::GameMode::Dialog)) {
            bt::Field3dInfo vi = bt::field3d_info();
            const r3d::Stats& st = r3d::stats();
            fprintf(g_trace, "V cam=%.3f,%.3f eye=%.2f,%.2f,%.2f yaw=%.3f walk=%.2f models=%d culled=%d statics=%d culled=%d near=%d strips_culled=%d tris=%d xf=%d out=%d arena=%d batches=%d drops=%d sprites=%d rects=%d mode=%s\n",
                    vi.cam_x, vi.cam_z, vi.eye[0], vi.eye[1], vi.eye[2], vi.hero_yaw, vi.walk_weight, st.models, st.models_culled, st.statics, st.statics_culled,
                    st.statics_near, st.strips_culled,
                    st.tris, st.verts_xf, st.verts_out, host_arena_used(), host_batches(), host_drops(), host_sprites(), host_rects(), bt::game_mode_name());
        }
        if (story) story_trace(g_trace, n);
        if (bot && !bot_battles && bt::battle_outcome() != bt::BattleOutcome::None) break;
        static int linger = 0;
        if (route && story_done() && md == bt::GameMode::Field && ++linger > 90) break;   // the route is over
    }
#ifdef HOST_ALLOC_COUNT
    fprintf(g_trace, "D heap: %ld allocations inside game ticks and draws after the boot\n", g_alloc_count);
#endif
    if (route) {
        fprintf(g_trace, "E route %s: %d expectations, %d failures\n", story_done() ? "done" : "NOT FINISHED", story_expects(), story_failures());
        if (!story_done() || story_failures()) fprintf(stderr, "route %s: %s, %d failures\n", route, story_done() ? "done" : "not finished", story_failures());
    }
    if (tlog) { turnlog_end(tlog); fclose(tlog); }
    if (rec) fclose(rec);
    fclose(g_trace);
    return 0;
}
