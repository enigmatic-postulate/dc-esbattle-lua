// game.cpp - see game.h
#include "game.h"
#include "battle.h"
#include "battle3d.h"
#include "chars.h"
#include "dialog_ui.h"
#include "encounters.h"
#include "field.h"
#include "field3d.h"
#include "font.h"
#include "icons.h"
#include "input.h"
#include "lights.h"
#include "portrait.h"
#include "progress.h"
#include "script.h"
#include "text.h"
#include "uitext.h"
#include "transitions.h"
#include <stdio.h>
#include <string.h>

namespace bt {

static GameMode   g_mode = GameMode::Field;
static PartyState g_party;
static int        g_fade_in = 0;        // T5 / T6 / harness start: ticks of fade-in remaining (the mode frozen meanwhile)
static int        g_trans = 0;          // ticks into ToBattle / ToField
static int        g_pending = -1;       // field entity being fought
static bool       g_battle_test = false;
static int        g_retries = 0;
static char       g_error[96] = "";
// ---- slice 3D-6
static int  g_mode_t = 0;               // ticks since the mode began
static int  g_tid = 0;                  // the catalogue transition shown (traces; T5/T6 run on the counters above)
static bool g_session = false;          // a game is in progress (the results card counts it)
static bool g_cleared = false;          // finished once this session (the title's crest)
static SessionStats g_stats;
static bool g_script_battle = false, g_boss_battle = false;   // the battle was started by a script (`battle`), a boss's
static bool g_retry_boss = false;       // the last loss was the boss's: the retry runs his intro at once
static const char* const BOSS_RETRY_SCRIPT = "warden_retry";   // the boss's short intro (data: barrow.txt)
static bool g_credits_from_title = false;
static int  g_title_t = 0;              // the orbit's clock
static bool g_hold_black = false;       // a script's `transition T11` left the screen black until the next mode
static bool g_skip = false;             // a cutscene skip's fast-forward runs this frame
// the exit / `map` command being carried out (T7)
struct MapGo { int map, x, y, dir, preset; bool walk; };
static MapGo g_mc;
// a script's `card` / `namecard`
struct Card { const char* text; int t, len; bool on, name; };
static Card g_card;
// The attract demo (GDD 5; decisions D10, BA5): the first minutes of a new game replayed from recorded inputs
// (romdisk/demo/attract.in: run-length "<ticks> <buttons hex> <stick x> <stick y>" lines, recorded by the host harness
// from tests/barrow/routes/attract.route - tools/make_attract.sh). The game is deterministic, so the same inputs play
// the same demo on every platform. Any button returns to the title.
const int ATTRACT_IDLE = 2400;                    // 40 s without input on the title
struct DemoRun { uint16_t n; uint8_t b; int8_t ax, ay; };
const int DEMO_RUNS = 1536;
static DemoRun g_demo[DEMO_RUNS];
static int g_ndemo = 0, g_demo_ticks = 0;
static bool g_attract = false;
static int g_demo_run = 0, g_demo_k = 0, g_demo_t = 0;

// The transition machine (T1, T2's fades, T7, T10, T11, the retry's fades): `out` ticks covering the screen, the
// swap (mid), `in` ticks uncovering it. Input is locked throughout; a fresh press is needed afterwards (GDD 4).
enum TransLook : uint8_t { TL_BLACK, TL_WHITE, TL_FLASH_WHITE };
struct Trans { int id, t, out, in; uint8_t look; void (*mid)(); bool swapped, active; };
static Trans g_tr;

GameMode game_mode() { return g_mode; }
const PartyState& game_party() { return g_party; }
int game_pending_entity() { return g_pending; }
const char* game_error() { return g_error; }
int game_retries() { return g_retries; }
int game_transition() { return g_tr.active ? g_tr.id : g_tid; }
int game_transition_tick() { return g_tr.active ? g_tr.t : g_trans; }
const SessionStats& game_stats() { return g_stats; }
bool game_cleared() { return g_cleared; }
bool game_skipping() { return g_skip; }
bool game_attract() { return g_attract; }

const char* game_mode_name() {
    switch (g_mode) {
    case GameMode::Field:     return "field";
    case GameMode::ToBattle:  return "to_battle";
    case GameMode::Battle:    return "battle";
    case GameMode::ToField:   return "to_field";
    case GameMode::Death:     return "death";
    case GameMode::Error:     return "error";
    case GameMode::Dialog:    return "dialog";
    case GameMode::Title:     return "title";
    case GameMode::Card:      return "card";
    case GameMode::MapChange: return "map_change";
    case GameMode::Credits:   return "credits";
    case GameMode::Results:   return "results";
    case GameMode::ToTitle:   return "to_title";
    case GameMode::Attract:   return "attract";
    }
    return "?";
}

static void set_mode(GameMode m) { g_mode = m; g_mode_t = 0; }

static void trans_start(int id, TransLook look, int out, int in, void (*mid)()) {
    g_tr.id = id; g_tr.t = 0; g_tr.out = out; g_tr.in = in; g_tr.look = look; g_tr.mid = mid; g_tr.swapped = false; g_tr.active = true;
    input_set_lock(true);
    if (out <= 0) { g_tr.swapped = true; if (mid) mid(); }
}

static void trans_tick() {
    if (!g_tr.active) return;
    ++g_tr.t;
    if (!g_tr.swapped && g_tr.t >= g_tr.out) { g_tr.swapped = true; g_hold_black = false; if (g_tr.mid) g_tr.mid(); }
    if (g_tr.t >= g_tr.out + g_tr.in) {
        g_tr.active = false;
        input_set_lock(false);
        input_reset();                               // a held button does not carry over (GDD 4)
    }
}

// The party as the rules see it: the current HP and stats here, max HP / potions / skills from Progress
// (one source of truth, docs/NEXT_SLICE_DIALOG.md 5.6).
static void sync_party() {
    const Progress& p = progress();
    g_party.hero_hp_max = p.max_hp;
    if (g_party.hero_hp > g_party.hero_hp_max) g_party.hero_hp = g_party.hero_hp_max;
    g_party.potions = p.potions;
    g_party.skills = progress_skill_fire() ? SKILL_FIRE : 0u;
}

void game_objective_text(char* out, int cap) {
    const char* raw = script_objective(progress().objective);
    int n = 0;
    for (const char* s = raw; *s && n < cap - 1;) {
        if (!strncmp(s, "{patrols}", 9)) { out[n++] = (char)('0' + progress_patrols()); s += 9; continue; }
        out[n++] = *s++;
    }
    out[n] = '\0';
}

// Progress changed (a script command, a won battle, a retry, a map load): `if` entities, markers, exits, glows, the party.
static void refresh() {
    const FieldState& f = field_state();
    for (int i = 0; i < f.ents.size(); ++i) {
        field_set_hidden(i, !script_entity_visible(i, progress()));
        field3d_set_marker(i, -1);
    }
    int nm = 0;
    const Marker* m = script_markers(&nm);
    for (int k = nm - 1; k >= 0; --k)                          // the first rule that holds wins
        if (script_marker_here(m[k]) && cond_eval(script_cond(m[k].cond), m[k].cond_len, progress())) field3d_set_marker(m[k].entity, m[k].kind);
    for (int i = 0; i < f.exits.size(); ++i) field_set_exit_open(i, script_exit_open(i, progress()));
    for (int i = 0; i < f.glows.size(); ++i) field_set_glow(i, script_glow_on(i, progress()));
    sync_party();
}

// ---------------------------------------------------------------------------- maps (slice 3D-6)
static int current_map() { const int m = script_map_find(field_state().name); return m < 0 ? 0 : m; }

// Loads map `id` (under a fade): the field, its scene and ground, the scripts' binding, its light (or `preset`).
static bool load_map(int id, int preset) {
    char path[64];
    snprintf(path, sizeof path, "/rd/maps/%s.txt", script_map_name(id));
    eng::boot_log(path);
    if (!field_load(path)) { snprintf(g_error, sizeof g_error, "MAP LOAD ERROR"); set_mode(GameMode::Error); return false; }
    field3d_init();                                            // the scene of this map (props, ground)
    script_set_map(id);
    progress().map = (uint8_t)id;
    if (preset < 0) { preset = light_find(field_state().light); if (preset < 0) preset = 0; }
    field3d_set_light(preset);
    return true;
}

// ---------------------------------------------------------------------------- script host hooks
static void host_heal() { sync_party(); g_party.hero_hp = g_party.hero_hp_max; }
static void host_checkpoint(int id) {
    int tx, ty;
    field_hero_tile(&tx, &ty);
    checkpoint_save(id, tx, ty, field_state().hero.dir);
}
static void host_face(int actor, int v) {
    const int n = field_state().ents.size();
    if (actor == 0) {
        if (v & 0x100) { const int t = (v & 0xFF) - 1; if (t >= 0 && t < n) field_hero_face_entity(t); }
        else { static const int DX[4] = { 0, 0, -1, 1 }, DY[4] = { 1, -1, 0, 0 }; field_hero_face(DX[v & 3] * 256, DY[v & 3] * 256); }
        return;
    }
    const int i = actor - 1;
    if (i < 0 || i >= n) return;
    if (!(v & 0x100)) { field_entity_face(i, (Dir)(v & 3)); return; }
    const int t = v & 0xFF;
    if (t == 0) { field_entity_face_hero(i); return; }
    int ax, ay, bx, by;
    field_entity_pixel(i, &ax, &ay);
    field_entity_pixel(t - 1, &bx, &by);
    field_entity_face(i, dir_toward(bx - ax, by - ay));
}
static void host_anim(int actor, int clip) { field3d_play(actor, clip); }
static void host_emote(int actor, int kind) { field3d_emote(actor, kind); }
static void host_fade(bool out, int ticks) { dialog_fade(out, ticks); }
static void host_letterbox(bool on) { dialog_letterbox(on); }
static void host_changed() { refresh(); }
static void host_move(int actor, int x, int y, int flags) {
    if (flags & MOVE_REL) field_script_move_rel(actor, x, y, (flags & MOVE_RUN) != 0);
    else field_script_move(actor, x, y, (flags & MOVE_RUN) != 0);
}
static void host_place(int actor, int x, int y, int dir) { field_script_place(actor, x, y, (Dir)dir); }
static void host_cam(int shot, int ticks) { field3d_cam_shot(shot, ticks); }
static void start_battle(int entity, bool from_script, bool boss);
static void host_battle(int actor, bool boss) { start_battle(actor - 1, true, boss); }
static void start_map_change(int map, int x, int y, int dir, int preset, bool walk);
static void host_map(int map, int x, int y, int dir, int preset) { start_map_change(map, x, y, dir, preset, false); }
static void host_light(int preset) { field3d_set_light(preset); }
static void host_light_lerp(int from, int to, int ticks) { field3d_light_lerp(from, to, ticks); }
static void host_namecard(const char* text, int ticks) { g_card = Card{ text, 0, ticks, true, true }; eng::audio_event("sting_boss_name"); }
static void host_card(const char* text, int ticks) { g_card = Card{ text, 0, ticks, true, false }; }
static void host_transition(int id);
static void host_screen(int kind);
static bool host_busy() {
    if (field_script_moving() || field3d_cam_busy() || g_card.on) return true;
    if (g_tr.active && !g_tr.swapped) return true;
    return g_mode == GameMode::ToBattle || g_mode == GameMode::Battle || g_mode == GameMode::ToField || g_mode == GameMode::MapChange ||
           g_mode == GameMode::Credits || g_mode == GameMode::Results || g_mode == GameMode::Death;
}

static void load_demo();
static void load_common_assets() {
    icons_init();
    font_init();
    text_init();                                  // the dialog font
    ui_init();                                    // the UI font and icons (docs/UI_ART.md); the old ones are the fallback
    chars_init();
    battle_init();
    trans_init();
    screens_init();
    load_demo();
}

static void load_demo() {
    g_ndemo = 0; g_demo_ticks = 0;
    static char buf[24 * 1024];
    if (eng::file_read("/rd/demo/attract.in", buf, sizeof buf) < 0) { eng::boot_log("attract: no /rd/demo/attract.in (no demo)"); return; }
    for (char* line = buf; line && *line;) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        if (line[0] != '#' && line[0]) {
            unsigned n = 0, b = 0;
            int ax = 0, ay = 0;
            if (sscanf(line, "%u %x %d %d", &n, &b, &ax, &ay) >= 2 && n > 0 && n < 65536 && g_ndemo < DEMO_RUNS) {
                g_demo[g_ndemo++] = DemoRun{ (uint16_t)n, (uint8_t)b, (int8_t)(ax < -128 ? -128 : (ax > 127 ? 127 : ax)),
                                             (int8_t)(ay < -128 ? -128 : (ay > 127 ? 127 : ay)) };
                g_demo_ticks += (int)n;
            }
        }
        line = next;
    }
    char m[96];
    snprintf(m, sizeof m, "attract: %d runs, %d ticks", g_ndemo, g_demo_ticks);
    eng::boot_log(m);
}

static void fail_init(const char* what) {
    snprintf(g_error, sizeof g_error, "%s", what);
    set_mode(GameMode::Error);
}

// Runs a script by name (instant ones finish in this tick). Returns true if it blocked: Dialog mode.
static bool run_script_index(int s, int npc) {
    if (s < 0) return false;
    if (!script_start(s)) { refresh(); return false; }
    dialog_begin(npc);
    if (npc >= 0) field3d_dialog_begin(npc);
    set_mode(GameMode::Dialog);
    field3d_show_prompt(false);
    return true;
}
static bool run_script(const char* name, int npc) { return run_script_index(script_find(name), npc); }

static void session_begin() {
    memset(&g_stats, 0, sizeof g_stats);
    g_session = true;
    g_retries = 0;
}

// A new game on the current map (the village): C0 at its spawn, the new_game script's objective.
static void new_progress() {
    const FieldState& f = field_state();
    progress_new_game(f.spawn_x, f.spawn_y, f.spawn_dir, 0, current_map());
    const int ng = script_find("new_game");
    if (ng >= 0 && script_start(ng)) { eng::dbg_print("script: new_game must not block (pages/choices)"); script_abort(); }
    checkpoint_save(0, f.spawn_x, f.spawn_y, f.spawn_dir);
    field3d_reset();
    refresh();
    g_party = party_default();
    sync_party();
    g_party.hero_hp = g_party.hero_hp_max;
}

static bool boot_common(const char* map_path) {
    eng::boot_log("2D assets (icons, font, battle sprites)");
    load_common_assets();
    g_party = party_default();
    g_battle_test = false;
    g_error[0] = '\0';
    eng::boot_log("light presets");
    lights_load();
    eng::boot_log("portrait slots");
    portrait_init();
    eng::boot_log("field_load");
    if (!field_load(map_path)) { fail_init("MAP LOAD ERROR"); return false; }
    eng::boot_log("field3d_init");
    field3d_init();
    eng::boot_log("scripts");
    if (!script_load(field_state().name)) { fail_init("SCRIPT ERROR"); return false; }
    portrait_preload();                           // every speaker's portraits into RAM (no file I/O in play)
    static const ScriptHost HOST = { host_heal, host_checkpoint, host_face, host_anim, host_emote, host_fade, host_letterbox, host_changed,
                                     host_move, host_place, host_cam, host_battle, host_map, host_light, host_light_lerp, host_namecard,
                                     host_card, host_transition, host_screen, host_busy };
    script_set_host(HOST);
    const int lp = light_find(field_state().light);
    field3d_set_light(lp < 0 ? 0 : lp);
    return true;
}

bool game_init(const char* map_path) {
    if (!boot_common(map_path)) return false;
    // a new game = checkpoint C0: stage 0, no potions, 20 HP, the new_game script's objective
    session_begin();
    new_progress();
    eng::boot_log("game_init: field ready");
    set_mode(GameMode::Field);
    g_fade_in = T_FADE_IN;
    return true;
}

// ---------------------------------------------------------------------------- title (GDD 5)
static void enter_title() {
    g_session = false;
    script_abort();
    dialog_reset();
    g_card.on = false;
    field3d_cam_release();
    const int village = script_map_find("village");
    if (village >= 0 && current_map() != village) { if (!load_map(village, -1)) return; }
    const int dusk = light_find("dusk");
    field3d_set_light(dusk < 0 ? 0 : dusk);
    const FieldState& f = field_state();
    field_restore(f.spawn_x, f.spawn_y, f.spawn_dir, 0);
    field3d_reset();
    field3d_show_actors(false, false);
    title_enter(g_cleared);
    g_title_t = 0;
    set_mode(GameMode::Title);
    eng::audio_event("music_title");
}

bool game_boot() {
    if (!boot_common("/rd/maps/village.txt")) return false;
    enter_title();
    trans_start(T1, TL_BLACK, 0, trans_def(T1).in, nullptr);
    eng::boot_log("game_boot: title");
    return true;
}

// T2: at black, the new game's state; then the text card (Card mode), then the intro fades in.
static void t2_mid() {
    const int village = script_map_find("village");
    if (village >= 0 && current_map() != village) load_map(village, -1);
    const int day = light_find(field_state().light);
    field3d_set_light(day < 0 ? 0 : day);
    field3d_show_actors(true, true);
    field3d_clear_camera();
    const FieldState& f = field_state();
    field_restore(f.spawn_x, f.spawn_y, f.spawn_dir, 0);
    session_begin();
    new_progress();
    set_mode(GameMode::Card);
}

static void start_intro() {
    // the intro cutscene under T2's fade-in; a map without one goes straight to the field
    if (!run_script("intro", -1)) set_mode(GameMode::Field);
    trans_start(T2, TL_BLACK, 0, 30, nullptr);
    eng::audio_event("music_village");
}

// The demo starts as the host harness starts a new game (game_init): the village at its spawn, C0, the 12-tick fade in;
// its recorded inputs then replay from their second tick (the first one ran under this tick's input).
static void attract_mid() {
    t2_mid();
    g_session = false;
    set_mode(GameMode::Field);
    g_fade_in = T_FADE_IN;
    g_attract = true;
    g_demo_run = 0; g_demo_k = 0; g_demo_t = 1;
    if (g_ndemo > 0 && ++g_demo_k >= g_demo[0].n) { g_demo_k = 0; g_demo_run = 1; }   // line 1 ran under this tick's input
    eng::audio_event("music_village");
}

static void attract_end_mid() { g_attract = false; input_feed(false, 0, 0, 0); g_tid = 0; enter_title(); }

// Before input_update: the demo's next input (the pad is only read for "any button": back to the title).
static void attract_feed() {
    if (!g_attract || g_tr.active) return;
    const bool done = g_demo_run >= g_ndemo;
    if (input_pad_any() || done) {
        input_feed(false, 0, 0, 0);
        script_abort();
        trans_start(T11, TL_BLACK, 30, trans_def(T1).in, attract_end_mid);
        return;
    }
    const DemoRun& r = g_demo[g_demo_run];
    input_feed(true, r.b, r.ax, r.ay);
    ++g_demo_t;
    if (++g_demo_k >= r.n) { g_demo_k = 0; ++g_demo_run; }
}

static void credits_from_title_mid() {
    g_credits_from_title = true;
    g_tid = 0;
    credits_enter();
    set_mode(GameMode::Credits);
    eng::audio_event("music_credits");
}

static void tick_title() {
    ++g_title_t;
    float e[3], l[3], fov;
    title_orbit(g_title_t, e, l, &fov);
    field3d_set_camera(e, l, fov);
    field3d_tick();
    if (g_tr.active) return;                               // T1 (the logo waits for it)
    const TitleAction a = title_tick();
    if (a == TITLE_NONE && title_idle_ticks() >= ATTRACT_IDLE && g_ndemo > 0) {   // 40 s idle: the attract demo
        trans_start(T11, TL_BLACK, 30, 0, attract_mid);
        return;
    }
    if (a == TITLE_NEW_GAME) { eng::audio_event("menu_confirm"); trans_start(T2, TL_BLACK, 30, 0, t2_mid); }
    else if (a == TITLE_CREDITS) { eng::audio_event("menu_confirm"); trans_start(T11, TL_BLACK, 30, 30, credits_from_title_mid); }
}

static void tick_card() {
    // T2: hold 15 on black, then the text card 90 (A skips the card), then the intro fades in
    field3d_tick();
    if (g_mode_t >= 15 && input_pressed(eng::KEY_FIRE)) { input_consume(eng::KEY_FIRE); g_mode_t = 105; }
    if (g_mode_t >= 105) start_intro();
}

// ---------------------------------------------------------------------------- battles
static void start_battle(int entity, bool from_script, bool boss) {
    g_pending = entity; g_trans = 0;
    g_script_battle = from_script; g_boss_battle = boss;
    set_mode(GameMode::ToBattle);
    g_tid = T5;
    field3d_show_prompt(false);
    eng::audio_event(boss ? "music_boss" : "music_battle");
}

static void battle_finish_stats(bool won) {
    const BattleTally& t = battle_tally();
    g_stats.hero_turns += t.hero_turns; g_stats.dmg_dealt += t.dmg_dealt; g_stats.dmg_taken += t.dmg_taken;
    g_stats.potions_used += t.potions_used;
    if (won) ++g_stats.battles_won;
}

// A won battle: HP carries over, the potions go back, the foe is removed (its defeated bit, its win flag).
static void apply_win() {
    g_party.hero_hp = battle_hero_hp();
    progress().potions = (uint8_t)battle_potions();
    field_defeat(g_pending);
    const int map = progress().map < MAX_MAPS ? progress().map : 0;
    if (g_pending >= 0 && g_pending < 16) progress().defeated[map] |= (uint16_t)(1u << g_pending);
    progress_set_flag(script_entity_win_flag(g_pending), true);
    g_pending = -1;
    refresh();
}

// T10's swap: the barrow at dawn under the white; the script (the ending) resumes.
static void t10_mid() {
    set_mode(GameMode::Dialog);
    eng::audio_event("music_dawn");
}

static DeathTip death_tip() {
    if (battle_potions() >= MAX_POTIONS) return TIP_POTIONS;
    return g_boss_battle ? TIP_BOSS : TIP_PATROL;
}

static void tick_battle() {
    if (g_fade_in > 0) { --g_fade_in; if (!g_fade_in) g_tid = 0; return; }
    battle_update();
    if (g_battle_test) return;
    const BattleOutcome o = battle_outcome();
    if (o == BattleOutcome::Won && g_boss_battle) {
        // E1 ended by itself (no press); T10 into the ending
        battle_finish_stats(true);
        apply_win();
        g_boss_battle = false; g_script_battle = false;
        trans_start(T10, TL_FLASH_WHITE, trans_def(T10).out, trans_def(T10).in, t10_mid);
        set_mode(GameMode::ToField);                      // the battle stays on screen under the white
        g_tid = 0;
        return;
    }
    if (o == BattleOutcome::Won) { g_trans = 0; set_mode(GameMode::ToField); g_tid = T6; eng::audio_event("sting_victory"); return; }
    if (o == BattleOutcome::Lost) {
        battle_finish_stats(false);
        g_retry_boss = g_boss_battle;
        death_enter(death_tip());
        set_mode(GameMode::Death);
        g_tid = T9;
        eng::audio_event("sting_death");
    }
}

static void tick_to_battle() {
    field3d_tick();                                        // keeps idling/settling under the flashes and the iris
    if (++g_trans >= trans_def(T5).out) {
        const EncounterDef* enc = encounter_by_id(field_state().ents[g_pending].encounter);
        sync_party();                                      // max HP, potions and skills from Progress
        battle_set_auto(true, g_boss_battle);
        battle3d_set_short_intro(g_boss_battle);           // BA4: the field's T8 showed the name card
        battle_begin(*enc, g_party);                       // validated at map load
        set_mode(GameMode::Battle);
        g_fade_in = T_FADE_IN;
    }
}

static void tick_to_field() {
    if (g_tr.active) return;                               // T10 (the boss): the transition machine swaps
    if (++g_trans >= T_FADE_OUT) {
        g_fade_in = T_FADE_IN;
        battle_finish_stats(true);
        apply_win();
        if (script_running()) {                            // a script's battle: it goes on
            set_mode(GameMode::Dialog);
        } else {
            set_mode(GameMode::Field);
            run_script("after_battle", -1);                // pages: Dialog mode over the field camera
        }
        g_script_battle = false;
    }
}

// ---------------------------------------------------------------------------- death and retry (GDD 13)
static void retry_mid() {
    // the last checkpoint's map, tile, stage, flags, potions and defeated foes; HP full
    script_abort();
    g_card.on = false;
    checkpoint_restore();
    const Progress& p = progress();
    if (p.map != current_map()) { const int m = p.map; if (!load_map(m, -1)) return; progress().map = (uint8_t)m; }
    const Progress& q = progress();
    field_restore(q.cp_x, q.cp_y, (Dir)q.cp_dir, q.defeated[q.map < MAX_MAPS ? q.map : 0]);
    dialog_reset();
    refresh();
    g_party.hero_hp = g_party.hero_hp_max;
    field3d_reset();
    ++g_retries;
    ++g_stats.retries;
    set_mode(GameMode::Field);
    g_tid = 0;
    // GDD 13: a retry after the boss's win over the hero goes straight to his short intro (romdisk/scripts/barrow.txt)
    // (only if the checkpoint is on the boss's map: Brom's rest after the waystone puts the retry in the village)
    if (g_retry_boss) {
        g_retry_boss = false;
        const int rs = script_find(BOSS_RETRY_SCRIPT);
        if (rs >= 0 && script_def(rs).map == script_current_map()) run_script_index(rs, -1);
    }
}

static void to_title_mid() { g_tid = 0; enter_title(); }

static void tick_death() {
    // T9: 0.5x for 30 ticks (the battle's presentation only: its rules have ended), then on
    if (g_mode_t >= 30 || (g_mode_t & 1)) battle_update();
    if (g_tr.active) return;
    const DeathAction a = death_tick(g_mode_t);
    if (a == DEATH_RETRY) { eng::audio_event("menu_confirm"); trans_start(T6, TL_BLACK, 30, 20, retry_mid); }
    else if (a == DEATH_TITLE) { eng::audio_event("menu_confirm"); trans_start(T11, TL_BLACK, trans_def(T11).out, trans_def(T1).in, to_title_mid); }
}

// ---------------------------------------------------------------------------- map changes (T7)
static void map_mid() {
    if (!load_map(g_mc.map, g_mc.preset)) return;
    const Progress& p = progress();
    field_restore(g_mc.x, g_mc.y, (Dir)g_mc.dir, p.defeated[p.map < MAX_MAPS ? p.map : 0]);
    refresh();
    field3d_reset();
    if (g_mc.walk) {                                      // 30 ticks of scripted walk-in, one tile (GDD 6 T7)
        static const int DX[4] = { 0, 0, -1, 1 }, DY[4] = { 1, -1, 0, 0 };
        field_script_move(0, g_mc.x + DX[g_mc.dir & 3], g_mc.y + DY[g_mc.dir & 3], false);
    }
    eng::audio_event(current_map() == script_map_find("barrow") ? "music_barrow" : "music_village");
}

static void start_map_change(int map, int x, int y, int dir, int preset, bool walk) {
    g_mc = MapGo{ map, x, y, dir, preset, walk };
    set_mode(GameMode::MapChange);
    field3d_show_prompt(false);
    trans_start(T7, TL_BLACK, trans_def(T7).out, trans_def(T7).in, map_mid);
}

static void start_exit(int e) {
    const Exit& x = field_state().exits[e];
    const int m = script_map_find(x.map);
    if (m < 0) return;
    eng::audio_event("gate");
    start_map_change(m, x.tx, x.ty, (int)x.dir, -1, true);
}

static void tick_map_change() {
    if (g_tr.swapped) field_script_tick();                // the walk-in
    field3d_tick();
    if (g_tr.active || field_script_moving()) return;
    set_mode(script_running() ? GameMode::Dialog : GameMode::Field);
}

// ---------------------------------------------------------------------------- the script's screens
static void host_transition(int id) {
    const TransDef& d = trans_def(id);
    if (id == T11) { trans_start(T11, TL_BLACK, d.out, 0, nullptr); g_hold_black = true; return; }
    if (id == T10) { trans_start(T10, TL_FLASH_WHITE, d.out, d.in, nullptr); return; }
    trans_start(id, TL_BLACK, d.out, d.in, nullptr);
}

static void host_screen(int kind) {
    if (kind == SCREEN_CREDITS) {
        g_credits_from_title = false;
        credits_enter();
        field3d_show_actors(false, false);                // the flyover shows the village (and keeps the frame cheap)
        set_mode(GameMode::Credits);
        eng::audio_event("music_credits");
        return;
    }
    if (kind == SCREEN_RESULTS) { g_session = false; results_enter(g_stats); set_mode(GameMode::Results); return; }
    // title: the session ends; the crest shows (the ending is the only script that says `title`)
    g_cleared = true;
    enter_title();
    trans_start(T1, TL_BLACK, 0, trans_def(T1).in, nullptr);
    g_hold_black = false;
}

static void tick_credits() {
    float e[3], l[3], fov;
    credits_flyover(g_mode_t, e, l, &fov);
    field3d_set_camera(e, l, fov);
    field3d_tick();
    if (g_tr.active) return;
    if (credits_tick(g_mode_t)) {
        if (g_credits_from_title) { trans_start(T11, TL_BLACK, 30, trans_def(T1).in, to_title_mid); return; }
        set_mode(GameMode::Dialog);                        // the ending goes on (results)
    }
}

static void tick_results() {
    field3d_tick();
    if (results_tick(g_mode_t)) { eng::audio_event("menu_confirm"); set_mode(GameMode::Dialog); }
}

// ---------------------------------------------------------------------------- field and dialog
static void tick_field() {
    dialog_tick_closing();                                 // T4: the bars slide out while the hero already moves
    if (g_fade_in > 0) { --g_fade_in; if (!g_fade_in) g_tid = 0; field3d_show_prompt(false); field3d_tick(); return; }
    field_update();
    field3d_tick();                                        // cosmetic only (camera, facing, animation)
    const int c = field_take_contact();
    if (c >= 0) { start_battle(c, false, false); return; }
    const int tr = field_take_trigger();
    if (tr >= 0) {
        // slice 3D-6: a trigger's script (its `if` and `once` hold); the hero's feet entered its rectangle
        const Progress& p = progress();
        const int map = p.map < MAX_MAPS ? p.map : 0;
        const Trigger& t = field_state().triggers[tr];
        if (script_trigger_active(tr, p) && !(t.once && (p.trig[map] >> tr & 1u))) {
            if (t.once) progress().trig[map] |= (uint16_t)(1u << tr);
            if (run_script_index(script_trigger_script(tr), -1)) return;
        }
    }
    const int ex = field_take_exit();
    if (ex >= 0) { start_exit(ex); return; }
    const int t = field_talk_target();
    field3d_show_prompt(t >= 0);
    if (t >= 0 && input_pressed(eng::KEY_FIRE)) {          // a fresh press on an NPC in range: T3
        input_consume(eng::KEY_FIRE);
        field_entity_face_hero(t);                         // the NPC turns to the hero, the hero to the NPC
        field_hero_face_entity(t);
        const int s = script_entity_talk(t);
        if (s >= 0 && script_start(s)) {
            dialog_begin(t);
            field3d_dialog_begin(t);
            set_mode(GameMode::Dialog);
            field3d_show_prompt(false);
            eng::audio_event("talk");
        } else {
            refresh();
        }
    }
}

// Start fast-forwards a skippable cutscene (the intro, the boss intro, the ending) to its next choice or its end:
// the same ticks run, with A pressed every other tick and nothing drawn, so every state change happens exactly as
// when watched (D7). It stops at a choice, a battle, the credits and the results.
static void tick_core();
static void skip_cutscene() {
    g_skip = true;
    input_set_lock(false);
    for (int i = 0; i < 6000; ++i) {
        const bool choice = script_blocking_pc() >= 0 && script_op(script_blocking_pc()).code == SC_CHOICE;
        if (!script_running() || choice) break;
        if (g_mode != GameMode::Dialog && g_mode != GameMode::MapChange) break;
        input_feed(true, (i & 1) ? (1u << eng::KEY_FIRE) : 0u, 0, 0);
        tick_core();
    }
    input_feed(false, 0, 0, 0);
    g_skip = false;
    if (!g_tr.active) trans_start(T4, TL_BLACK, 0, 12, nullptr);   // a short fade in over the cut
    eng::audio_event("skip");
}

static void tick_dialog() {
    // the world is frozen (no field_update): patrols hold, only the view and the cutscene's moves animate
    if (g_fade_in > 0) --g_fade_in;
    if (!g_skip && !g_tr.active && input_pressed(eng::KEY_START) && script_skippable() &&
        !(script_blocking_pc() >= 0 && script_op(script_blocking_pc()).code == SC_CHOICE)) {
        input_consume(eng::KEY_START);
        skip_cutscene();
        return;
    }
    script_tick();                                         // waits, fades, cutscene activities
    if (g_mode != GameMode::Dialog) return;                // the script started a battle, a map change, a screen
    portrait_tick();
    field_script_tick();
    if (g_card.on && ++g_card.t >= g_card.len) g_card.on = false;
    if (!dialog_tick()) {                                  // T4: input returns at once
        set_mode(GameMode::Field);
        if (field3d_cam_scripted()) field3d_cam_shot(-1, 20);   // a cutscene's camera eases back to the follow camera
        refresh();
    }
    field3d_tick();
}

static void tick_core() {
    if (g_attract && !g_skip) attract_feed();
    input_update();
    trans_tick();
    ++g_mode_t;
    if (g_session && g_mode != GameMode::Title && g_mode != GameMode::Results) ++g_stats.play_ticks;
    switch (g_mode) {
    case GameMode::Field:     tick_field(); break;
    case GameMode::Dialog:    tick_dialog(); break;
    case GameMode::ToBattle:  tick_to_battle(); break;
    case GameMode::Battle:    tick_battle(); break;
    case GameMode::ToField:   tick_to_field(); break;
    case GameMode::Death:     tick_death(); break;
    case GameMode::Title:     tick_title(); break;
    case GameMode::Card:      tick_card(); break;
    case GameMode::MapChange: tick_map_change(); break;
    case GameMode::Credits:   tick_credits(); break;
    case GameMode::Results:   tick_results(); break;
    case GameMode::ToTitle:
    case GameMode::Attract:
    case GameMode::Error:     break;
    }
}

void game_tick() {
    tick_core();
    // the catalogue id shown for T5 / T6 / T9 (traces): over once their counters have run out
    if ((g_tid == T5 || g_tid == T6) && g_fade_in == 0 && g_mode != GameMode::ToBattle && g_mode != GameMode::ToField) g_tid = 0;
    if (g_tid == T9 && g_mode != GameMode::Death) g_tid = 0;
}

void game_init_battle_test(const char* encounter_id, int skills) {
    load_common_assets();
    g_party = party_default();
    g_battle_test = true;
    const EncounterDef* enc = encounter_by_id(encounter_id);
    if (!enc) { set_mode(GameMode::Error); return; }
    // the balance tests' "boss-ready" party (before slice 3D-5 the warden encounter carried these for the game too)
    if (enc->test_max_hp > 0) { g_party.hero_hp_max = enc->test_max_hp; g_party.hero_hp = enc->test_max_hp; }
    if (enc->test_potions > g_party.potions) g_party.potions = enc->test_potions;
    if (skills >= 0) g_party.skills = (uint32_t)skills;
    battle_set_auto(false, false);
    battle_begin(*enc, g_party);
    set_mode(GameMode::Battle);
    g_fade_in = 0;
}

// ---------------------------------------------------------------------------- drawing
static int trans_alpha() {
    if (!g_tr.active) return g_hold_black ? 255 : 0;
    if (g_tr.t < g_tr.out) return g_tr.out > 0 ? 255 * g_tr.t / g_tr.out : 255;
    const int k = g_tr.t - g_tr.out;
    return g_tr.in > 0 ? 255 * (g_tr.in - k) / g_tr.in : 0;
}

int game_fade_alpha() {
    if (g_mode == GameMode::ToBattle) return 255 * g_trans / trans_def(T5).out;
    if (g_mode == GameMode::ToField && !g_tr.active) return 255 * g_trans / T_FADE_OUT;
    if (g_fade_in > 0) return 255 * g_fade_in / T_FADE_IN;
    return trans_alpha();
}

static void draw_error() {
    eng::gfx_rect(0, 0, 640, 480, 0xFF301018, 0.5f);
    ui_text_px(320, 200, g_error[0] ? g_error : "LOAD ERROR", 21.0f, 0xFFFFCD75, 8.5f, UA_CENTRE);
    ui_text(UF_S, 320, 250, "SEE SERIAL LOG", 0xFFF4F4F4, 8.5f, 1.0f, UA_CENTRE);
}

static void draw_field_scene(bool hud) {
    field3d_draw();
    char obj[96];
    game_objective_text(obj, sizeof obj);
    field3d_draw_hud(g_party.hero_hp, g_party.hero_hp_max, progress().potions, progress_flag(quest_flags().has_token), obj,
                     hud && g_mode == GameMode::Field && !dialog_hides_hud() && !g_tr.active);
    dialog_draw();
    if (g_card.on) {
        if (g_card.name) namecard_draw(g_card.text, g_card.t, g_card.len);
        else card_draw(g_card.text, g_card.t, g_card.len, true);
    }
}

// The last 2 ticks before a transition's swap draw nothing but the fade's colour: the swap may rewrite a texture slot
// (the barrow's painted ground, texture_update_vq), which the PVR must not have been given in the last 2 frames.
static bool covered() {
    return g_tr.active && !g_tr.swapped && g_tr.look != TL_FLASH_WHITE && g_tr.out > 0 && g_tr.t >= g_tr.out - 2;
}

void game_draw() {
    if (covered()) {
        eng::gfx_rect(0, 0, 640, 480, 0xFF000000u, Z_TRANS);
        portrait_frame_end();
        return;
    }
    switch (g_mode) {
    case GameMode::Field:
    case GameMode::ToBattle:
    case GameMode::Dialog:
    case GameMode::MapChange:
        draw_field_scene(true);
        break;
    case GameMode::Battle:
    case GameMode::ToField:
        eng::set_fog(false, 0, 0.0f, 0.0f);                // the battle's arenas have their own light, no fog
        battle_draw();
        break;
    case GameMode::Death:
        eng::set_fog(false, 0, 0.0f, 0.0f);
        if (g_tr.active && g_tr.swapped) draw_field_scene(false);   // the retry's fade-in is on the field
        else { battle_draw(); death_draw(g_mode_t); }
        break;
    case GameMode::Title:
        field3d_draw();
        title_draw();
        break;
    case GameMode::Card:
        t2_card_draw(g_mode_t < 15 ? 0 : g_mode_t - 15);   // on black: no 3D under it
        break;
    case GameMode::Credits:
        field3d_draw();
        credits_draw(g_mode_t);
        break;
    case GameMode::Results:
        field3d_draw();
        results_draw(g_mode_t);
        break;
    case GameMode::ToTitle:
    case GameMode::Attract:
        field3d_draw();
        break;
    case GameMode::Error:    draw_error(); break;
    }
    if (g_attract && ((g_mode_t / 30) & 1) == 0) ui_text_px(560, 24, "DEMO", 21.0f, 0xFFFFD070u, 8.6f);   // GDD 5
    portrait_frame_end();
    // transitions: T5 (flashes + iris), the T6/T1/T2/T7/T11 fades, T10's white
    if (g_mode == GameMode::ToBattle) trans_draw_flash_iris(g_trans, trans_def(T5).out - 8);
    else if (g_tr.active && g_tr.look == TL_FLASH_WHITE) {
        // T10: a white flash (8 t), then white over 40 t; after the swap, from white
        if (!g_tr.swapped) {
            if (g_tr.t < 8) trans_draw_fade(0xFFFFFF, g_tr.t < 4 ? 230 : 120);
            else trans_draw_fade(0xFFFFFF, 120 + 135 * (g_tr.t - 8) / (g_tr.out - 8 > 0 ? g_tr.out - 8 : 1));
        } else {
            trans_draw_fade(0xFFFFFF, trans_alpha());
        }
    } else {
        const int a = game_fade_alpha();
        if (a > 0) trans_draw_fade(g_tr.active && g_tr.look == TL_WHITE ? 0xFFFFFF : 0x000000, a);
    }
}

} // namespace bt
