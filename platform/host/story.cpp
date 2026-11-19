// story.cpp - host harness only (slice 3D-5, task 0.4): a closed-loop story driver. It plays a route
// file through the real inputs - walking with the stick along BFS paths, talking to NPCs, answering
// choices, chasing patrols (the battle bot plays the battles) - and writes the quest's state to the
// trace: Q lines (Progress, when it changes), G lines (the dialog UI, every dialog tick), N lines (mode changes),
// I lines (each tick's buttons and stick) and E lines
// (route expectations). Closed loop: it looks at the game's state each tick and presses what the
// state needs, so routes survive timing changes (patrol positions, battle lengths).
//
// Route file (one step per line, '#' comments):
//   talk <npc>              walk next to the NPC, face it, press A; the step ends when the dialog does
//   chase <enemy>           walk into the enemy (re-planned as it patrols); ends back on the field after
//                           the battle (and its after_battle dialog, or the game over + retry)
//   goto <x> <y>            walk to the tile
//   choose <k> [<k> ...]    answers for the next choices, in order (0 = the first option; 'cancel')
//   policy <bot policy>     the battle bot's policy from now on (battle_bot.cpp; e.g. smart, guard)
//   wait <ticks>            stand still
//   press <a|b|up|down>     one press (a tick down, a tick up)
//   open <npc>              like talk, but the step ends as soon as the dialog opens (UI tests)
//   manual                  from now on the driver does not answer dialogs: press / hold / stick / wait
//                           steps run in the Dialog mode too (UI tests: exact timings)
//   hold <button> <ticks>   hold a button (a, b, up, down) for that many ticks
//   stick <x> <y> <ticks>   hold the stick
//   pace <ticks>            dialogs from now on: let each page type out fully, then wait this long before A
//                           (0, the default: press A every other tick - complete, then advance)
//   expect <key>=<value>... checked when reached: stage potions max_hp hp mode retries objective
//                           <flag>=0|1, visible:<id>=0|1, marker:<id>=!|...|-, pos=<x>,<y> (hero tile),
//                           at_cp=1 (the hero on the last checkpoint's tile), cp=<id>
//   slice 3D-6 (--boot runs; the frame around the game):
//   newgame                 on the title: Start, then A on NEW GAME (the T2 card and the intro follow)
//   title_credits           on the title: Start, down, A (the credits roll, then the title again)
//   skip                    Start once, as soon as a skippable cutscene runs (or the credits roll)
//   death retry|title       the next death screen's answer (default: retry)
//   until <mode>            wait (pressing nothing, answering dialogs as usual) until the game is in that mode
//   exit <id>               walk into the map's exit; ends on the other map (after T7 and the walk-in)
//   goto also ends when a script starts on the way (a trigger: the road block, the boss ring)
//   expect also knows: map=<name> cleared=0|1 transition=<Tn|->
// In a dialog the driver presses A every other tick (it completes, then advances each page) and answers
// choices from the queue, else from --choices (first | last | cancel | k,k,...). On the death screen it waits for the
// options and presses A (retry) or down + A (title); on the results card A.
#include "../../src/dialog_ui.h"
#include "../../src/field.h"
#include "../../src/field3d.h"
#include "../../src/game.h"
#include "../../src/portrait.h"
#include "../../src/progress.h"
#include "../../src/script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bot_init(const char* policy);
extern uint32_t g_buttons;   // host_engine.cpp: this tick's buttons
extern int      g_axis[2];

namespace {

enum Btn { B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_A = 16, B_B = 32, B_Y = 64, B_START = 128 };
enum StepKind { S_TALK, S_CHASE, S_GOTO, S_CHOOSE, S_POLICY, S_WAIT, S_PRESS, S_EXPECT, S_PACE, S_OPEN, S_MANUAL, S_HOLD, S_STICK,
                S_NEWGAME, S_SKIP, S_DEATH, S_TITLE_CREDITS, S_UNTIL, S_EXIT };
struct Step { StepKind kind; char arg[160]; int x, y; int line; };

const int MAX_STEPS = 256;
Step g_steps[MAX_STEPS];
int  g_nsteps = 0, g_si = 0;
// current step's state
int  g_phase = 0, g_t = 0, g_last_plan = -1000;
bool g_saw_dialog = false, g_saw_battle = false;
// the path being walked (tile centres)
int  g_path[512][2];
int  g_plen = 0, g_pi = 0;
// choices
int  g_queue[64];
int  g_nq = 0, g_qi = 0;
char g_policy[64] = "first";
int  g_pol_list[32], g_npol = 0, g_poli = 0;
// dialog driving
int  g_prev_press = 0;
int  g_pace = 0, g_ready_t = 0, g_ready_key = -1;   // pace: ticks to wait once a page / choice is complete
bool g_manual = false;                               // UI tests: the route's steps drive dialogs too
int  g_fail = 0, g_expects = 0;
char g_last_q[1024] = "";
FILE* g_trace_file = nullptr;
int  g_tick = 0;
char g_prev_mode[16] = "";
int  g_battles = 0;
bool g_death_title = false;                          // slice 3D-6: `death title` - the next death screen returns to the title
int  g_death_t = 0;                                  // ticks on the death screen
char g_exit_from[32] = "";                           // `exit`: the map the step started on
int  g_prev_trans = 0;

int entity_index(const char* id) {
    const bt::FieldState& f = bt::field_state();
    for (int i = 0; i < f.ents.size(); ++i) if (!strcmp(f.ents[i].id, id)) return i;
    return -1;
}

void report(const char* fmt, const char* a, const char* b) {
    char m[400];
    snprintf(m, sizeof m, fmt, a, b);
    if (g_trace_file) fprintf(g_trace_file, "E %s\n", m);
    fprintf(stderr, "story: %s\n", m);
}

// ---------------------------------------------------------------------------- path planning
// 4-connected BFS over walkable tiles. Enemies other than `target` block their tile, their patrol segment
// and a 1-tile margin (no accidental battles); NPCs block the tiles they stand on.
bool blocked_tile(int x, int y, int target) {
    if (!bt::cell_walkable(x, y)) return true;
    const bt::FieldState& f = bt::field_state();
    for (int i = 0; i < f.ents.size(); ++i) {
        const bt::Entity& e = f.ents[i];
        if (!e.alive || e.hidden || i == target) continue;
        const int m = e.kind == bt::EntityKind::Enemy ? 1 : 0;
        int x0 = e.w.tx, y0 = e.w.ty, x1 = e.w.tx, y1 = e.w.ty;
        if (e.w.moving) { x0 = x0 < e.w.nx ? x0 : e.w.nx; x1 = x1 > e.w.nx ? x1 : e.w.nx; y0 = y0 < e.w.ny ? y0 : e.w.ny; y1 = y1 > e.w.ny ? y1 : e.w.ny; }
        if (e.patrol && e.kind == bt::EntityKind::Enemy) {     // NPCs stop for the hero: only their tiles count
            const int ax = e.home_x, ay = e.home_y, bx = e.home_x + e.pdx * e.plen, by = e.home_y + e.pdy * e.plen;
            x0 = x0 < (ax < bx ? ax : bx) ? x0 : (ax < bx ? ax : bx); x1 = x1 > (ax > bx ? ax : bx) ? x1 : (ax > bx ? ax : bx);
            y0 = y0 < (ay < by ? ay : by) ? y0 : (ay < by ? ay : by); y1 = y1 > (ay > by ? ay : by) ? y1 : (ay > by ? ay : by);
        }
        if (x >= x0 - m && x <= x1 + m && y >= y0 - m && y <= y1 + m) return true;
    }
    return false;
}

// goal: a tile set predicate. Returns the path length (0: unreachable).
template <class G> int plan(G goal, int target) {
    const bt::FieldState& f = bt::field_state();
    int sx, sy;
    bt::field_hero_tile(&sx, &sy);
    static int prev[64 * 64];
    static int queue[64 * 64];
    for (int i = 0; i < f.w * f.h; ++i) prev[i] = -2;
    int qh = 0, qt = 0;
    queue[qt++] = sy * f.w + sx;
    prev[sy * f.w + sx] = -1;
    int found = -1;
    while (qh < qt) {
        const int c = queue[qh++];
        const int cx = c % f.w, cy = c / f.w;
        if (goal(cx, cy)) { found = c; break; }
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (const auto& d : D) {
            const int nx = cx + d[0], ny = cy + d[1];
            if (nx < 0 || ny < 0 || nx >= f.w || ny >= f.h) continue;
            const int n = ny * f.w + nx;
            if (prev[n] != -2) continue;
            if (!goal(nx, ny) && blocked_tile(nx, ny, target)) continue;
            if (goal(nx, ny) && !bt::cell_walkable(nx, ny)) continue;
            prev[n] = c;
            queue[qt++] = n;
        }
    }
    g_plen = 0; g_pi = 0;
    if (found < 0) return 0;
    int tmp[512], n = 0;
    for (int c = found; c >= 0 && n < 512; c = prev[c]) tmp[n++] = c;
    for (int k = n - 1; k >= 0; --k) { g_path[g_plen][0] = tmp[k] % f.w; g_path[g_plen][1] = tmp[k] / f.w; ++g_plen; }
    return g_plen;
}

void hero_pos(float* x, float* y) {
    const bt::Hero& h = bt::field_state().hero;
    *x = (float)(h.x + (bt::HERO_BOX_X0 + bt::HERO_BOX_X1) / 2 * bt::SUBPX) / (float)(bt::TILE_PX * bt::SUBPX);
    *y = (float)(h.y + (bt::HERO_BOX_Y0 + bt::HERO_BOX_Y1) / 2 * bt::SUBPX) / (float)(bt::TILE_PX * bt::SUBPX);
}

void steer_to(float tx, float ty, int* ax, int* ay) {
    float hx, hy;
    hero_pos(&hx, &hy);
    const float dx = tx - hx, dy = ty - hy;
    float d = dx * dx + dy * dy;
    if (d < 1e-6f) { *ax = *ay = 0; return; }
    d = __builtin_sqrtf(d);
    *ax = (int)(dx / d * 127.0f); *ay = (int)(dy / d * 127.0f);
}

// Follows the path; true when at its end.
bool follow(int* ax, int* ay) {
    float hx, hy;
    hero_pos(&hx, &hy);
    while (g_pi < g_plen) {
        const float cx = g_path[g_pi][0] + 0.5f, cy = g_path[g_pi][1] + 0.5f;
        if ((cx - hx) * (cx - hx) + (cy - hy) * (cy - hy) < 0.13f * 0.13f) { ++g_pi; continue; }
        steer_to(cx, cy, ax, ay);
        return false;
    }
    *ax = *ay = 0;
    return true;
}

void next_step() { ++g_si; g_phase = 0; g_t = 0; g_last_plan = -1000; g_saw_dialog = g_saw_battle = false; g_plen = g_pi = 0; }

// ---------------------------------------------------------------------------- expectations
bool check_expect(const char* kv) {
    char key[48], val[96];
    const char* eq = strchr(kv, '=');
    if (!eq) return false;
    snprintf(key, sizeof key, "%.*s", (int)(eq - kv), kv);
    snprintf(val, sizeof val, "%s", eq + 1);
    const bt::Progress& p = bt::progress();
    char got[128] = "";
    if (!strcmp(key, "stage")) snprintf(got, sizeof got, "%d", p.stage);
    else if (!strcmp(key, "potions")) snprintf(got, sizeof got, "%d", p.potions);
    else if (!strcmp(key, "max_hp")) snprintf(got, sizeof got, "%d", p.max_hp);
    else if (!strcmp(key, "hp")) snprintf(got, sizeof got, "%d", bt::game_party().hero_hp);
    else if (!strcmp(key, "mode")) snprintf(got, sizeof got, "%s", bt::game_mode_name());
    else if (!strcmp(key, "retries")) snprintf(got, sizeof got, "%d", bt::game_retries());
    else if (!strcmp(key, "cp")) snprintf(got, sizeof got, "%d", p.cp_id);
    else if (!strcmp(key, "objective")) { bt::game_objective_text(got, sizeof got); for (char* c = got; *c; ++c) if (*c == ' ') *c = '_'; }
    else if (!strcmp(key, "pos")) { int x, y; bt::field_hero_tile(&x, &y); snprintf(got, sizeof got, "%d,%d", x, y); }
    else if (!strcmp(key, "map")) snprintf(got, sizeof got, "%s", bt::field_state().name);
    else if (!strncmp(key, "walkable:", 9)) { int x = -1, y = -1; sscanf(key + 9, "%d,%d", &x, &y); snprintf(got, sizeof got, "%d", bt::cell_walkable(x, y) ? 1 : 0); }
    else if (!strcmp(key, "cleared")) snprintf(got, sizeof got, "%d", bt::game_cleared() ? 1 : 0);
    else if (!strcmp(key, "transition")) { const int t = bt::game_transition(); if (t) snprintf(got, sizeof got, "T%d", t); else snprintf(got, sizeof got, "-"); }
    else if (!strcmp(key, "at_cp")) { int x, y; bt::field_hero_tile(&x, &y); snprintf(got, sizeof got, "%d", x == p.cp_x && y == p.cp_y ? 1 : 0); }
    else if (!strncmp(key, "visible:", 8)) {
        const int i = entity_index(key + 8);
        if (i < 0) return false;
        const bt::Entity& e = bt::field_state().ents[i];
        snprintf(got, sizeof got, "%d", e.alive && !e.hidden ? 1 : 0);
    } else if (!strncmp(key, "marker:", 7)) {
        const int i = entity_index(key + 7);
        int nm = 0;
        const bt::Marker* m = bt::script_markers(&nm);
        snprintf(got, sizeof got, "-");
        for (int k = 0; k < nm; ++k)
            if (bt::script_marker_here(m[k]) && m[k].entity == i && bt::cond_eval(bt::script_cond(m[k].cond), m[k].cond_len, p)) { snprintf(got, sizeof got, "%s", m[k].kind == bt::MARK_QUEST ? "!" : "..."); break; }
    } else {
        const int f = bt::progress_flag_index(key);
        if (f < 0) { snprintf(got, sizeof got, "?unknown-key"); }
        else snprintf(got, sizeof got, "%d", bt::progress_flag(f) ? 1 : 0);
    }
    const bool ok = !strcmp(got, val);
    char m[300];
    snprintf(m, sizeof m, "%s (got %s)", kv, got);
    if (g_trace_file) fprintf(g_trace_file, "E expect %s %s\n", ok ? "ok" : "FAIL", m);
    if (!ok) { ++g_fail; fprintf(stderr, "story: expect FAIL %s\n", m); }
    ++g_expects;
    return ok;
}

// ---------------------------------------------------------------------------- dialog / choices
bool g_use_b = false;      // answer with B (the cancel rule) instead of moving the cursor there

int choice_answer(int n_opts, int cancel) {
    g_use_b = false;
    if (g_qi < g_nq) {
        const int k = g_queue[g_qi++];
        if (k == -1) { g_use_b = cancel != bt::NO_CANCEL; return cancel != bt::NO_CANCEL ? cancel : n_opts - 1; }
        return k < n_opts ? k : n_opts - 1;
    }
    if (!strcmp(g_policy, "last")) return n_opts - 1;
    if (!strcmp(g_policy, "cancel")) { g_use_b = cancel != bt::NO_CANCEL; return cancel != bt::NO_CANCEL ? cancel : 0; }
    if (g_npol) { const int k = g_pol_list[g_poli < g_npol ? g_poli++ : g_npol - 1]; return k < n_opts ? k : n_opts - 1; }
    return 0;
}

int g_choice_target = -1, g_choice_pc = -1;

uint32_t drive_dialog() {
    const bt::DialogInfo d = bt::dialog_info();
    bool may_press = g_prev_press == 0;
    if (g_pace > 0) {                                   // paced: a page types out, then it stays up `pace` ticks
        const int key = d.choice_pc >= 0 ? 10000 + d.choice_pc : d.page_pc;
        const bool ready = d.box && (d.choice_pc >= 0 || d.complete);
        if (!ready || key != g_ready_key) { g_ready_key = ready ? key : -1; g_ready_t = 0; may_press = false; }
        else if (++g_ready_t < g_pace) may_press = false;
    }
    if (d.choice_pc >= 0 && d.box) {
        if (d.choice_pc != g_choice_pc) { g_choice_pc = d.choice_pc; g_choice_target = choice_answer(d.n_opts, d.cancel); }
        if (!may_press) return 0;
        if (g_use_b) return B_B;                                 // "cancel": B picks the cancel option (GDD 7.3)
        if (d.sel < g_choice_target) return B_DOWN;
        if (d.sel > g_choice_target) return B_UP;
        return B_A;
    }
    g_choice_pc = -1;
    if (d.box && d.page_pc >= 0 && may_press) return B_A;
    return 0;
}

} // namespace

static uint32_t button_of(const char* n) {
    return !strcmp(n, "a") ? B_A : !strcmp(n, "b") ? B_B : !strcmp(n, "up") ? B_UP : !strcmp(n, "down") ? B_DOWN :
           !strcmp(n, "left") ? B_LEFT : !strcmp(n, "right") ? B_RIGHT : !strcmp(n, "y") ? B_Y : 0;
}

bool story_load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return false; }
    char line[256];
    int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ++ln;
        char* h = strchr(line, '#');
        if (h) *h = 0;
        char w[32] = "";
        int off = 0;
        if (sscanf(line, "%31s %n", w, &off) < 1) continue;
        if (g_nsteps >= MAX_STEPS) break;
        Step& s = g_steps[g_nsteps];
        memset(&s, 0, sizeof s);
        s.line = ln;
        char* rest = line + off;
        size_t L = strlen(rest);
        while (L && (rest[L - 1] == '\n' || rest[L - 1] == ' ' || rest[L - 1] == '\r')) rest[--L] = 0;
        snprintf(s.arg, sizeof s.arg, "%s", rest);
        if (!strcmp(w, "talk")) s.kind = S_TALK;
        else if (!strcmp(w, "chase")) s.kind = S_CHASE;
        else if (!strcmp(w, "goto")) { s.kind = S_GOTO; if (sscanf(rest, "%d %d", &s.x, &s.y) != 2) { fprintf(stderr, "%s:%d: goto x y\n", path, ln); fclose(f); return false; } }
        else if (!strcmp(w, "choose")) s.kind = S_CHOOSE;
        else if (!strcmp(w, "policy")) s.kind = S_POLICY;
        else if (!strcmp(w, "wait")) { s.kind = S_WAIT; s.x = atoi(rest); }
        else if (!strcmp(w, "press")) s.kind = S_PRESS;
        else if (!strcmp(w, "expect")) s.kind = S_EXPECT;
        else if (!strcmp(w, "pace")) { s.kind = S_PACE; s.x = atoi(rest); }
        else if (!strcmp(w, "open")) s.kind = S_OPEN;
        else if (!strcmp(w, "manual")) s.kind = S_MANUAL;
        else if (!strcmp(w, "newgame")) s.kind = S_NEWGAME;
        else if (!strcmp(w, "title_credits")) s.kind = S_TITLE_CREDITS;
        else if (!strcmp(w, "skip")) s.kind = S_SKIP;
        else if (!strcmp(w, "death")) s.kind = S_DEATH;
        else if (!strcmp(w, "until")) s.kind = S_UNTIL;
        else if (!strcmp(w, "exit")) s.kind = S_EXIT;
        else if (!strcmp(w, "hold")) { s.kind = S_HOLD; char b[16]; if (sscanf(rest, "%15s %d", b, &s.x) != 2) { fprintf(stderr, "%s:%d: hold <button> <ticks>\n", path, ln); fclose(f); return false; } snprintf(s.arg, sizeof s.arg, "%s", b); }
        else if (!strcmp(w, "stick")) { s.kind = S_STICK; if (sscanf(rest, "%d %d %d", &s.x, &s.y, &s.line) != 3) { fprintf(stderr, "%s:%d: stick <x> <y> <ticks>\n", path, ln); fclose(f); return false; } }
        else { fprintf(stderr, "%s:%d: unknown step %s\n", path, ln, w); fclose(f); return false; }
        ++g_nsteps;
    }
    fclose(f);
    return true;
}

void story_set_choices(const char* policy) {
    snprintf(g_policy, sizeof g_policy, "%s", policy);
    g_npol = 0;
    if (policy[0] >= '0' && policy[0] <= '9') {
        char buf[64];
        snprintf(buf, sizeof buf, "%s", policy);
        for (char* t = strtok(buf, ","); t && g_npol < 32; t = strtok(nullptr, ",")) g_pol_list[g_npol++] = atoi(t);
    }
}

bool story_done() { return g_si >= g_nsteps; }
int  story_failures() { return g_fail; }
int  story_expects() { return g_expects; }
int  story_step() { return g_si; }

// Buttons and stick for this tick (not used while a battle runs: the bot plays those).
uint32_t story_buttons(int* ax, int* ay) {
    *ax = *ay = 0;
    uint32_t btn = 0;
    const bt::GameMode md = bt::game_mode();
    ++g_t;
    if (g_si < g_nsteps && g_steps[g_si].kind == S_OPEN && g_phase == 2 && md == bt::GameMode::Dialog) next_step();
    const bool manual_step = g_manual && g_si < g_nsteps &&
        (g_steps[g_si].kind == S_PRESS || g_steps[g_si].kind == S_HOLD || g_steps[g_si].kind == S_STICK ||
         g_steps[g_si].kind == S_WAIT || g_steps[g_si].kind == S_EXPECT);
    // slice 3D-6: steps that only set something up run in any mode
    if (g_si < g_nsteps && g_steps[g_si].kind == S_WAIT && md == bt::GameMode::Title) {   // slice 3D-6: the title's idle
        if (g_t >= g_steps[g_si].x) next_step();
        g_prev_press = 0;
        return 0;
    }
    while (g_si < g_nsteps && (g_steps[g_si].kind == S_DEATH || g_steps[g_si].kind == S_POLICY || g_steps[g_si].kind == S_PACE ||
                               g_steps[g_si].kind == S_CHOOSE)) {
        Step& s = g_steps[g_si];
        if (s.kind == S_DEATH) g_death_title = !strcmp(s.arg, "title");
        else if (s.kind == S_POLICY) bot_init(s.arg);
        else if (s.kind == S_PACE) g_pace = s.x;
        else {
            char buf[160];
            snprintf(buf, sizeof buf, "%s", s.arg);
            for (char* t = strtok(buf, " "); t && g_nq < 64; t = strtok(nullptr, " ")) g_queue[g_nq++] = !strcmp(t, "cancel") ? -1 : atoi(t);
        }
        next_step();
    }
    if (md != bt::GameMode::Death) g_death_t = 0;
    // slice 3D-6: a chase whose battle was lost and left for the title (the death screen's RETURN TO TITLE) is over
    if (g_si < g_nsteps && g_steps[g_si].kind == S_CHASE && g_saw_battle && md == bt::GameMode::Title) next_step();
    const StepKind cur = g_si < g_nsteps ? g_steps[g_si].kind : S_MANUAL;
    if (cur == S_UNTIL && (!strcmp(g_steps[g_si].arg, bt::game_mode_name()) || (!strcmp(g_steps[g_si].arg, "attract") && bt::game_attract())))
        next_step();
    if (bt::game_attract()) {                                         // slice 3D-6: the demo plays itself; a press would end it
        if (g_si < g_nsteps && g_steps[g_si].kind == S_PRESS) { const uint32_t b = button_of(g_steps[g_si].arg); next_step(); g_prev_press = 1; return b; }
        g_prev_press = 0;
        return 0;
    }

    if (g_si < g_nsteps && g_steps[g_si].kind == S_EXPECT && md != bt::GameMode::Field && md != bt::GameMode::Dialog &&
        md != bt::GameMode::Battle && !manual_step) {
        char buf[160];                                                // expectations outside the field (the title, results)
        snprintf(buf, sizeof buf, "%s", g_steps[g_si].arg);
        for (char* t = strtok(buf, " "); t; t = strtok(nullptr, " ")) check_expect(t);
        next_step();
        g_prev_press = 0;
        return 0;
    }
    if (md == bt::GameMode::Title) {
        static int ready = 0;                                        // ticks the title has taken input (T1 over, the logo in)
        ready = bt::game_transition() ? 0 : ready + 1;
        if ((cur == S_NEWGAME || cur == S_TITLE_CREDITS) && ready > 50) {   // Start (opens the menu), [down], A
            if (g_phase == 0 && g_prev_press == 0) { btn = B_START; g_phase = 1; g_t = 0; }
            else if (g_phase == 1 && g_t > 20) { if (cur == S_TITLE_CREDITS) btn = B_DOWN; g_phase = 2; g_t = 0; }
            else if (g_phase == 2 && g_t > 4) { btn = B_A; next_step(); }
        }
        g_prev_press = btn ? 1 : 0;
        return btn;
    }
    if (md == bt::GameMode::Credits) {
        if (cur == S_SKIP && g_t > 30 && g_prev_press == 0) { btn = B_START; next_step(); }
        g_prev_press = btn ? 1 : 0;
        return btn;
    }
    if (md == bt::GameMode::Results) {
        btn = (g_prev_press == 0 && (g_t % 8) == 0) ? B_A : 0;
        g_prev_press = btn ? 1 : 0;
        return btn;
    }
    if (md == bt::GameMode::Card || md == bt::GameMode::MapChange) { g_prev_press = 0; return 0; }
    if (md == bt::GameMode::Dialog && cur == S_SKIP && bt::script_skippable() && g_prev_press == 0 && bt::game_transition() == 0 &&
        !(bt::script_blocking_pc() >= 0 && bt::script_op(bt::script_blocking_pc()).code == bt::SC_CHOICE)) {
        next_step();
        g_prev_press = 1;
        return B_START;
    }
    if (md == bt::GameMode::Dialog && cur == S_GOTO) next_step();   // slice 3D-6: a trigger's script ended the walk
    if (md == bt::GameMode::Dialog && !g_manual) {
        g_saw_dialog = true;
        btn = drive_dialog();
    } else if (md == bt::GameMode::Dialog && !manual_step) {
        g_saw_dialog = true;                                         // manual: nothing pressed between steps
    } else if (md == bt::GameMode::Death && !manual_step) {
        // the options take input from tick 160 (GDD 13): retry = A; title = down, then A
        ++g_death_t;
        if (g_death_title && g_death_t == 170) btn = B_DOWN;
        else if (g_death_t >= 176 && g_prev_press == 0 && (g_death_t % 4) == 0) { btn = B_A; if (g_death_title) g_death_title = false; }
    } else if ((md == bt::GameMode::Field || manual_step) && g_si < g_nsteps) {
        Step& s = g_steps[g_si];
        switch (s.kind) {
        case S_EXPECT: {
            char buf[160];
            snprintf(buf, sizeof buf, "%s", s.arg);
            for (char* t = strtok(buf, " "); t; t = strtok(nullptr, " ")) check_expect(t);
            next_step();
            break;
        }
        case S_CHOOSE: {
            char buf[160];
            snprintf(buf, sizeof buf, "%s", s.arg);
            for (char* t = strtok(buf, " "); t && g_nq < 64; t = strtok(nullptr, " ")) g_queue[g_nq++] = !strcmp(t, "cancel") ? -1 : atoi(t);
            next_step();
            break;
        }
        case S_POLICY: bot_init(s.arg); next_step(); break;
        case S_PACE: g_pace = s.x; next_step(); break;
        case S_WAIT: if (g_t >= s.x) next_step(); break;
        case S_PRESS:
            if (g_phase == 0) { g_phase = 1; btn = button_of(s.arg); }
            else next_step();
            break;
        case S_HOLD:
            if (g_t <= s.x) btn = button_of(s.arg);
            else next_step();
            break;
        case S_STICK:
            if (g_t <= s.line) { *ax = s.x; *ay = s.y; }
            else next_step();
            break;
        case S_MANUAL: g_manual = true; next_step(); break;
        case S_NEWGAME: case S_SKIP: case S_DEATH: case S_TITLE_CREDITS: case S_UNTIL: break;   // slice 3D-6: other modes
        case S_EXIT: {
            const bt::FieldState& f = bt::field_state();
            if (g_phase == 0) { snprintf(g_exit_from, sizeof g_exit_from, "%s", f.name); g_phase = 1; }
            if (strcmp(g_exit_from, f.name)) { next_step(); break; }           // arrived on the other map
            int e = -1;
            for (int i = 0; i < f.exits.size(); ++i) if (!strcmp(f.exits[i].id, s.arg)) e = i;
            if (e < 0) { report("exit %s: no such exit on this map%s", s.arg, ""); ++g_fail; next_step(); break; }
            const bt::Exit& x = f.exits[e];
            if (g_t - g_last_plan > 60 || g_plen == 0) {
                g_last_plan = g_t;
                if (!plan([&](int tx, int ty) { return tx >= x.x && tx < x.x + x.w && ty >= x.y && ty < x.y + x.h; }, -1)) {
                    report("exit %s unreachable (closed?)%s", s.arg, ""); ++g_fail; next_step(); break;
                }
            }
            if (follow(ax, ay)) steer_to(x.x + x.w * 0.5f, x.y + x.h * 0.5f, ax, ay);   // push on into the exit
            if (g_t > 3000) { report("exit %s timed out%s", s.arg, ""); ++g_fail; next_step(); }
            break;
        }
        case S_OPEN: {
            const int npc = entity_index(s.arg);
            if (npc < 0) { report("open %s: no such entity%s", s.arg, ""); ++g_fail; next_step(); break; }
            if (g_plen == 0 && g_phase == 0) {
                const bt::Entity& e = bt::field_state().ents[npc];
                const int ex = e.w.tx, ey = e.w.ty;
                plan([&](int x, int y) { const int dx = x - ex, dy = y - ey; return dx * dx + dy * dy == 1 && !blocked_tile(x, y, -1); }, npc);
            }
            if (g_phase == 0 && (follow(ax, ay) || bt::field_talk_target() == npc)) g_phase = 1;
            if (g_phase == 1) {
                if (bt::field_talk_target() == npc && g_prev_press == 0) { btn = B_A; *ax = *ay = 0; g_phase = 2; }
                else if (bt::field_talk_target() != npc) {
                    int px, py;
                    bt::field_entity_pixel(npc, &px, &py);
                    steer_to((float)(px + 16) / 32.0f, (float)(py + 16) / 32.0f, ax, ay);
                    *ax /= 3; *ay /= 3;
                }
            }
            break;
        }
        case S_GOTO: {
            if (g_t - g_last_plan > 60 || g_plen == 0) {
                g_last_plan = g_t;
                const int gx = s.x, gy = s.y;
                if (!plan([&](int x, int y) { return x == gx && y == gy; }, -1)) { report("line %s: goto %s unreachable", "", s.arg); ++g_fail; next_step(); break; }
            }
            if (follow(ax, ay) || g_saw_dialog) next_step();            // slice 3D-6: a trigger's script ends it too
            if (g_t > 3000) { report("goto %s timed out%s", s.arg, ""); ++g_fail; next_step(); }
            break;
        }
        case S_TALK: {
            const int npc = entity_index(s.arg);
            if (npc < 0) { report("talk %s: no such entity%s", s.arg, ""); ++g_fail; next_step(); break; }
            if (g_saw_dialog) { next_step(); break; }                    // the dialog ran and ended
            const bt::Entity& e = bt::field_state().ents[npc];
            if (g_phase == 0) {                                          // walk next to it
                if (g_t - g_last_plan > 40 || g_plen == 0) {
                    g_last_plan = g_t;
                    const int ex = e.w.tx, ey = e.w.ty;
                    if (!plan([&](int x, int y) { const int dx = x - ex, dy = y - ey; return dx * dx + dy * dy == 1 && !blocked_tile(x, y, -1); }, npc)) {
                        report("talk %s: no free tile next to it%s", s.arg, ""); ++g_fail; next_step(); break;
                    }
                }
                if (follow(ax, ay) || bt::field_talk_target() == npc) g_phase = 1;   // in range and facing: talk
            }
            if (g_phase == 1) {                                          // face it until the prompt points at it, then A
                if (bt::field_talk_target() == npc && g_prev_press == 0) { btn = B_A; *ax = *ay = 0; }
                else {
                    int px, py;
                    bt::field_entity_pixel(npc, &px, &py);
                    steer_to((float)(px + 16) / 32.0f, (float)(py + 16) / 32.0f, ax, ay);
                    // nudge gently: a short push faces it (the NPC blocks the hero)
                    *ax /= 3; *ay /= 3;
                }
            }
            if (g_t > 4000) {
                const bt::Hero& h = bt::field_state().hero;
                char why[160];
                snprintf(why, sizeof why, " (phase %d, target %d, hero %d,%d facing %d,%d, mode %s)", g_phase, bt::field_talk_target(), h.x, h.y, h.face_x, h.face_y, bt::game_mode_name());
                report("talk %s timed out%s", s.arg, why); ++g_fail; next_step();
            }
            break;
        }
        case S_CHASE: {
            const int en = entity_index(s.arg);
            if (en < 0) { report("chase %s: no such entity%s", s.arg, ""); ++g_fail; next_step(); break; }
            if (g_saw_battle) { next_step(); break; }                    // back on the field after the battle
            const bt::Entity& e = bt::field_state().ents[en];
            if (!e.alive || e.hidden) { report("chase %s: not on the field%s", s.arg, ""); ++g_fail; next_step(); break; }
            if (g_t - g_last_plan > 20 || g_plen == 0) {
                g_last_plan = g_t;
                const int ex = e.w.tx, ey = e.w.ty;
                plan([&](int x, int y) { return x == ex && y == ey; }, en);
            }
            if (follow(ax, ay)) {
                int px, py;
                bt::field_entity_pixel(en, &px, &py);
                steer_to((float)(px + 16) / 32.0f, (float)(py + 20) / 32.0f, ax, ay);
            }
            if (g_t > 6000) { report("chase %s timed out%s", s.arg, ""); ++g_fail; next_step(); }
            break;
        }
        }
    } else if (md == bt::GameMode::Battle || md == bt::GameMode::ToBattle) {
        if (g_si < g_nsteps && g_steps[g_si].kind == S_CHASE) g_saw_battle = true;
    }
    if (md != bt::GameMode::Dialog && md != bt::GameMode::Field) g_choice_pc = -1;
    g_prev_press = (btn & (B_A | B_B | B_UP | B_DOWN)) ? 1 : 0;
    return btn;
}

// Q (Progress, on change), G (dialog UI, each dialog tick), M (mode changes) lines.
void story_trace(FILE* t, int tick) {
    g_trace_file = t;
    g_tick = tick;
    const bt::Progress& p = bt::progress();
    char q[1024], obj[96];
    bt::game_objective_text(obj, sizeof obj);
    int n = snprintf(q, sizeof q, "stage=%d potions=%d max_hp=%d hp=%d flags=", p.stage, p.potions, p.max_hp, bt::game_party().hero_hp);
    bool any = false;
    for (int i = 0; i < bt::progress_flag_count(); ++i)
        if (bt::progress_flag(i)) { n += snprintf(q + n, sizeof q - (size_t)n, "%s%s", any ? "," : "", bt::progress_flag_name(i)); any = true; }
    if (!any) n += snprintf(q + n, sizeof q - (size_t)n, "-");
    n += snprintf(q + n, sizeof q - (size_t)n, " map=%d defeated=%x,%x cp=%d@%d,%d,%s retries=%d objective=\"%s\" markers=", p.map, p.defeated[0], p.defeated[1], p.cp_id, p.cp_x, p.cp_y,
                  bt::dir_name((bt::Dir)p.cp_dir), bt::game_retries(), obj);
    int nm = 0;
    const bt::Marker* m = bt::script_markers(&nm);
    bool anym = false;
    for (int k = 0; k < nm; ++k)
        if (bt::script_marker_here(m[k]) && bt::cond_eval(bt::script_cond(m[k].cond), m[k].cond_len, p)) {
            n += snprintf(q + n, sizeof q - (size_t)n, "%s%s:%s", anym ? "," : "", bt::field_state().ents[m[k].entity].id, m[k].kind == bt::MARK_QUEST ? "!" : "...");
            anym = true;
        }
    if (!anym) n += snprintf(q + n, sizeof q - (size_t)n, "-");
    // the raw bytes of Progress and of the last checkpoint's copy (tests/dialog: byte-exact restores)
    const unsigned char* pb = (const unsigned char*)&p;
    const unsigned char* cb = (const unsigned char*)&bt::progress_checkpoint();
    n += snprintf(q + n, sizeof q - (size_t)n, " raw=");
    for (size_t k = 0; k < sizeof(bt::Progress); ++k) n += snprintf(q + n, sizeof q - (size_t)n, "%02x", pb[k]);
    n += snprintf(q + n, sizeof q - (size_t)n, " cpraw=");
    for (size_t k = 0; k < sizeof(bt::Progress); ++k) n += snprintf(q + n, sizeof q - (size_t)n, "%02x", cb[k]);
    int htx, hty;
    bt::field_hero_tile(&htx, &hty);
    snprintf(q + n, sizeof q - (size_t)n, " mode=%s", bt::game_mode_name());
    if (strcmp(q, g_last_q)) { fprintf(t, "Q %d %s\n", tick, q); snprintf(g_last_q, sizeof g_last_q, "%s", q); }
    fprintf(t, "I %d %02x %d %d\n", tick, (unsigned)g_buttons, g_axis[0], g_axis[1]);   // this tick's input
    const int tr = bt::game_transition();
    if (tr != g_prev_trans) {                                         // slice 3D-6: each transition's start and end tick
        if (g_prev_trans) fprintf(t, "T %d end T%d\n", tick, g_prev_trans);
        if (tr) fprintf(t, "T %d start T%d\n", tick, tr);
        g_prev_trans = tr;
    }
    const char* md = bt::game_mode_name();
    if (strcmp(md, g_prev_mode) && !strcmp(md, "results")) {     // slice 3D-6: what the results card shows
        const bt::SessionStats& st = bt::game_stats();
        fprintf(t, "R %d play_ticks=%d battles_won=%d hero_turns=%d dmg_dealt=%d dmg_taken=%d potions_used=%d retries=%d rank=%d\n", tick,
                st.play_ticks, st.battles_won, st.hero_turns, st.dmg_dealt, st.dmg_taken, st.potions_used, st.retries, (int)bt::results_rank(st));
    }
    if (strcmp(md, g_prev_mode)) {
        if (!strcmp(md, "battle")) ++g_battles;
        fprintf(t, "N %d %s\n", tick, md);
        snprintf(g_prev_mode, sizeof g_prev_mode, "%s", md);
    }
    const bt::DialogInfo d = bt::dialog_info();
    if (d.open || d.closing) {
        int pv = -1;
        const int p0 = bt::portrait_loaded(0, &pv);
        int pv1 = -1;
        const int p1 = bt::portrait_loaded(1, &pv1);
        const int sp = bt::script_blocking_pc();
        fprintf(t, "G %d btn=%02x sy=%d open=%d closing=%d t=%d bars=%d cam=%d box=%d pc=%d op=%s page=%d shown=%d/%d pause=%d complete=%d marker=%d "
                   "choice=%d sel=%d opts=%d cancel=%d spk=%d var=%d slot=%d slide=%d port=%d slots=%d.%d,%d.%d uploads=%d fade=%d prompt=%d hud=%d npc=%d\n",
                tick, (unsigned)g_buttons, g_axis[1], d.open, d.closing, d.t, d.bars, d.cam_w1000, d.box, sp,
                sp >= 0 ? (bt::script_op(sp).code == bt::SC_PAGE ? "page" : bt::script_op(sp).code == bt::SC_SYS ? "sys" :
                           bt::script_op(sp).code == bt::SC_CHOICE ? "choice" : bt::script_op(sp).code == bt::SC_WAIT ? "wait" :
                           (bt::script_op(sp).code == bt::SC_FADE_OUT || bt::script_op(sp).code == bt::SC_FADE_IN) ? "fade" : "host") : "-",
                d.page_pc, d.shown, d.total, d.pause, d.complete, d.marker, d.choice_pc, d.sel, d.n_opts,
                d.cancel == bt::NO_CANCEL ? -1 : d.cancel, d.speaker, d.variant, d.slot, d.slide, d.portrait_shown, p0, pv, p1, pv1,
                bt::portrait_uploads(), d.fade, bt::field3d_info().prompt, bt::game_mode() == bt::GameMode::Field && !bt::dialog_hides_hud(),
                bt::dialog_npc());
    }
}
