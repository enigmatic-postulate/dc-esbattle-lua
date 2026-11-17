// game.cpp - see game.h
#include "game.h"
#include "battle.h"
#include "chars.h"
#include "dialog_ui.h"
#include "encounters.h"
#include "field.h"
#include "field3d.h"
#include "font.h"
#include "icons.h"
#include "input.h"
#include "portrait.h"
#include "progress.h"
#include "script.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

namespace bt {

static GameMode   g_mode = GameMode::Field;
static PartyState g_party;
static int        g_fade_in = 0;        // ticks of fade-in remaining (input frozen meanwhile)
static int        g_trans = 0;          // ticks into a fade-out transition
static int        g_pending = -1;       // field entity being fought
static int        g_over_ticks = 0;
static bool       g_battle_test = false;
static int        g_retries = 0;
static char       g_error[96] = "";

GameMode game_mode() { return g_mode; }
const PartyState& game_party() { return g_party; }
int game_pending_entity() { return g_pending; }
const char* game_error() { return g_error; }
int game_retries() { return g_retries; }

const char* game_mode_name() {
    switch (g_mode) {
    case GameMode::Field:    return "field";
    case GameMode::ToBattle: return "to_battle";
    case GameMode::Battle:   return "battle";
    case GameMode::ToField:  return "to_field";
    case GameMode::GameOver: return "game_over";
    case GameMode::Error:    return "error";
    case GameMode::Dialog:   return "dialog";
    }
    return "?";
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

// Progress changed (a script command, a won battle, a retry): `if` entities, markers, the party.
static void refresh() {
    const FieldState& f = field_state();
    for (int i = 0; i < f.ents.size(); ++i) {
        field_set_hidden(i, !script_entity_visible(i, progress()));
        field3d_set_marker(i, -1);
    }
    int nm = 0;
    const Marker* m = script_markers(&nm);
    for (int k = nm - 1; k >= 0; --k)                          // the first rule that holds wins
        if (cond_eval(script_cond(m[k].cond), m[k].cond_len, progress())) field3d_set_marker(m[k].entity, m[k].kind);
    sync_party();
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

static void load_common_assets() {
    icons_init();
    font_init();
    text_init();                                  // the dialog font; also the battle ring's lock icon
    chars_init();
    battle_init();
}

static void fail_init(const char* what) {
    snprintf(g_error, sizeof g_error, "%s", what);
    g_mode = GameMode::Error;
}

// Runs a script by name (instant ones finish in this tick). Returns true if it blocked: Dialog mode.
static bool run_script(const char* name, int npc) {
    const int s = script_find(name);
    if (s < 0) return false;
    if (!script_start(s)) return false;
    dialog_begin(npc);
    if (npc >= 0) field3d_dialog_begin(npc);
    g_mode = GameMode::Dialog;
    field3d_show_prompt(false);
    return true;
}

bool game_init(const char* map_path) {
    eng::boot_log("2D assets (icons, font, battle sprites)");
    load_common_assets();
    g_party = party_default();
    g_battle_test = false;
    g_error[0] = '\0';
    eng::boot_log("portrait slots");
    portrait_init();
    eng::boot_log("field_load");
    if (!field_load(map_path)) { fail_init("MAP LOAD ERROR"); return false; }
    eng::boot_log("field3d_init");
    field3d_init();
    eng::boot_log("scripts");
    if (!script_load(field_state().name)) { fail_init("SCRIPT ERROR"); return false; }
    portrait_preload();                           // every speaker's portraits into RAM (no file I/O in play)
    static const ScriptHost HOST = { host_heal, host_checkpoint, host_face, host_anim, host_emote, host_fade, host_letterbox, host_changed };
    script_set_host(HOST);
    // a new game = checkpoint C0: stage 0, no potions, 20 HP, the new_game script's objective
    progress_new_game(field_state().spawn_x, field_state().spawn_y, field_state().spawn_dir, 0);
    const int ng = script_find("new_game");
    if (ng >= 0 && script_start(ng)) { eng::dbg_print("script: new_game must not block (pages/choices)"); script_abort(); }
    checkpoint_save(0, field_state().spawn_x, field_state().spawn_y, field_state().spawn_dir);
    field3d_reset();
    refresh();
    g_party.hero_hp = g_party.hero_hp_max;
    eng::boot_log("game_init: field ready");
    g_mode = GameMode::Field;
    g_fade_in = T_FADE_IN;
    return true;
}

void game_init_battle_test(const char* encounter_id, int skills) {
    load_common_assets();
    g_party = party_default();
    g_battle_test = true;
    const EncounterDef* enc = encounter_by_id(encounter_id);
    if (!enc) { g_mode = GameMode::Error; return; }
    // the balance tests' "boss-ready" party (before slice 3D-5 the warden encounter carried these for the game too)
    if (enc->test_max_hp > 0) { g_party.hero_hp_max = enc->test_max_hp; g_party.hero_hp = enc->test_max_hp; }
    if (enc->test_potions > g_party.potions) g_party.potions = enc->test_potions;
    if (skills >= 0) g_party.skills = (uint32_t)skills;
    battle_begin(*enc, g_party);
    g_mode = GameMode::Battle;
    g_fade_in = 0;
}

static void retry_from_checkpoint() {
    // GDD 13 "Retry": the last checkpoint's map, tile, stage, flags, potions and defeated foes; HP full
    script_abort();
    checkpoint_restore();
    const Progress& p = progress();
    field_restore(p.cp_x, p.cp_y, (Dir)p.cp_dir, p.defeated);
    dialog_reset();
    refresh();
    g_party.hero_hp = g_party.hero_hp_max;
    field3d_reset();
    ++g_retries;
}

void game_tick() {
    input_update();
    switch (g_mode) {
    case GameMode::Field: {
        dialog_tick_closing();                           // T4: the bars slide out while the hero already moves
        if (g_fade_in > 0) { --g_fade_in; field3d_show_prompt(false); field3d_tick(); break; }
        field_update();
        field3d_tick();                                  // cosmetic only (camera, facing, animation)
        int c = field_take_contact();
        if (c >= 0) { g_pending = c; g_trans = 0; g_mode = GameMode::ToBattle; field3d_show_prompt(false); break; }
        const int t = field_talk_target();
        field3d_show_prompt(t >= 0);
        if (t >= 0 && input_pressed(eng::KEY_FIRE)) {    // a fresh press on an NPC in range: T3
            input_consume(eng::KEY_FIRE);
            field_entity_face_hero(t);                   // the NPC turns to the hero, the hero to the NPC
            field_hero_face_entity(t);
            const int s = script_entity_talk(t);
            if (s >= 0 && script_start(s)) {
                dialog_begin(t);
                field3d_dialog_begin(t);
                g_mode = GameMode::Dialog;
                field3d_show_prompt(false);
            } else {
                refresh();
            }
        }
        break;
    }
    case GameMode::Dialog:
        // the world is frozen (no field_update): patrols hold, only the view animates
        if (g_fade_in > 0) --g_fade_in;
        script_tick();                                   // waits and fades count down
        portrait_tick();
        if (!dialog_tick()) { g_mode = GameMode::Field; refresh(); }   // T4: input returns at once
        field3d_tick();
        break;
    case GameMode::ToBattle:
        field3d_tick();                                  // keeps idling/settling under the fade
        if (++g_trans >= T_FADE_OUT) {
            const EncounterDef* enc = encounter_by_id(field_state().ents[g_pending].encounter);
            sync_party();                                // max HP, potions and skills from Progress
            battle_begin(*enc, g_party);                 // validated at map load
            g_mode = GameMode::Battle;
            g_fade_in = T_FADE_IN;
        }
        break;
    case GameMode::Battle:
        if (g_fade_in > 0) { --g_fade_in; break; }
        battle_update();
        if (!g_battle_test && battle_outcome() != BattleOutcome::None) { g_trans = 0; g_mode = GameMode::ToField; }
        break;
    case GameMode::ToField:
        if (++g_trans >= T_FADE_OUT) {
            g_fade_in = T_FADE_IN;
            if (battle_outcome() == BattleOutcome::Won) {
                g_party.hero_hp = battle_hero_hp();      // HP carries over
                progress().potions = (uint8_t)battle_potions();
                field_defeat(g_pending);
                if (g_pending >= 0 && g_pending < 16) progress().defeated |= (uint16_t)(1u << g_pending);
                progress_set_flag(script_entity_win_flag(g_pending), true);
                g_pending = -1;
                refresh();
                g_mode = GameMode::Field;
                run_script("after_battle", -1);          // pages: Dialog mode over the field camera
            } else {
                g_mode = GameMode::GameOver;
                g_over_ticks = 0;
                g_pending = -1;
            }
        }
        break;
    case GameMode::GameOver:
        if (g_fade_in > 0) { --g_fade_in; break; }
        ++g_over_ticks;
        if (g_over_ticks >= 30 && input_pressed(eng::KEY_FIRE)) {
            input_consume(eng::KEY_FIRE);
            retry_from_checkpoint();
            g_mode = GameMode::Field;
            g_fade_in = T_FADE_IN;
        }
        break;
    case GameMode::Error:
        break;
    }
}

int game_fade_alpha() {
    if (g_mode == GameMode::ToBattle || g_mode == GameMode::ToField) return 255 * g_trans / T_FADE_OUT;
    if (g_fade_in > 0) return 255 * g_fade_in / T_FADE_IN;
    return 0;
}

static void draw_game_over() {
    eng::gfx_rect(0, 0, 640, 480, 0xFF100C18, 0.5f);
    font_draw_centered(320, 180, "GAME OVER", 5, 0xFFEF7D57, true, 8.5f);
    if (g_over_ticks >= 30 && (g_over_ticks / 20) % 2 == 0)
        font_draw_centered(320, 260, "PRESS A TO RETRY", 2, 0xFFF4F4F4, true, 8.5f);
}

static void draw_error() {
    eng::gfx_rect(0, 0, 640, 480, 0xFF301018, 0.5f);
    font_draw_centered(320, 200, g_error[0] ? g_error : "LOAD ERROR", 3, 0xFFFFCD75, true, 8.5f);
    font_draw_centered(320, 250, "SEE SERIAL LOG", 2, 0xFFF4F4F4, true, 8.5f);
}

void game_draw() {
    switch (g_mode) {
    case GameMode::Field:
    case GameMode::ToBattle:
    case GameMode::Dialog: {
        field3d_draw();
        char obj[96];
        game_objective_text(obj, sizeof obj);
        field3d_draw_hud(g_party.hero_hp, g_party.hero_hp_max, progress().potions, progress_flag(quest_flags().has_token), obj,
                         g_mode == GameMode::Field && !dialog_hides_hud());
        dialog_draw();
        break;
    }
    case GameMode::Battle:
    case GameMode::ToField:  battle_draw(); break;
    case GameMode::GameOver: draw_game_over(); break;
    case GameMode::Error:    draw_error(); break;
    }
    portrait_frame_end();
    int a = game_fade_alpha();
    if (a > 0) eng::gfx_rect_tr(0, 0, 640, 480, (uint32_t)a << 24, 9.0f);
}

} // namespace bt
