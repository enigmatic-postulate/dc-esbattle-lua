// game.cpp - see game.h
#include "game.h"
#include "battle.h"
#include "chars.h"
#include "encounters.h"
#include "field.h"
#include "font.h"
#include "icons.h"
#include "input.h"

namespace bt {

static GameMode   g_mode = GameMode::Field;
static PartyState g_party;
static int        g_fade_in = 0;        // ticks of fade-in remaining (input frozen meanwhile)
static int        g_trans = 0;          // ticks into a fade-out transition
static int        g_pending = -1;       // field entity being fought
static int        g_over_ticks = 0;
static bool       g_battle_test = false;

GameMode game_mode() { return g_mode; }
const PartyState& game_party() { return g_party; }
int game_pending_entity() { return g_pending; }

const char* game_mode_name() {
    switch (g_mode) {
    case GameMode::Field:    return "field";
    case GameMode::ToBattle: return "to_battle";
    case GameMode::Battle:   return "battle";
    case GameMode::ToField:  return "to_field";
    case GameMode::GameOver: return "game_over";
    case GameMode::Error:    return "error";
    }
    return "?";
}

static void load_common_assets() {
    icons_init();
    font_init();
    chars_init();
    battle_init();
}

bool game_init(const char* map_path) {
    load_common_assets();
    g_party = party_default();
    g_battle_test = false;
    if (!field_load(map_path)) { g_mode = GameMode::Error; return false; }
    g_mode = GameMode::Field;
    g_fade_in = T_FADE_IN;
    return true;
}

void game_init_battle_test(const char* encounter_id) {
    load_common_assets();
    g_party = party_default();
    g_battle_test = true;
    const EncounterDef* enc = encounter_by_id(encounter_id);
    if (!enc) { g_mode = GameMode::Error; return; }
    battle_begin(*enc, g_party);
    g_mode = GameMode::Battle;
    g_fade_in = 0;
}

void game_tick() {
    input_update();
    switch (g_mode) {
    case GameMode::Field: {
        if (g_fade_in > 0) { --g_fade_in; break; }
        field_update();
        int c = field_take_contact();
        if (c >= 0) { g_pending = c; g_trans = 0; g_mode = GameMode::ToBattle; }
        break;
    }
    case GameMode::ToBattle:
        if (++g_trans >= T_FADE_OUT) {
            const EncounterDef* enc = encounter_by_id(field_state().ents[g_pending].encounter);
            battle_begin(*enc, g_party);    // validated at map load
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
            if (battle_outcome() == BattleOutcome::Won) {
                g_party.hero_hp = battle_hero_hp();      // HP carries over
                field_defeat(g_pending);
                g_mode = GameMode::Field;
            } else {
                g_mode = GameMode::GameOver;
                g_over_ticks = 0;
            }
            g_pending = -1;
            g_fade_in = T_FADE_IN;
        }
        break;
    case GameMode::GameOver:
        if (g_fade_in > 0) { --g_fade_in; break; }
        ++g_over_ticks;
        if (g_over_ticks >= 30 && input_pressed(eng::KEY_FIRE)) {
            g_party = party_default();                   // full HP
            field_reset();                               // spawn + every entity restored
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
    font_draw_centered(320, 200, "MAP LOAD ERROR", 3, 0xFFFFCD75, true, 8.5f);
    font_draw_centered(320, 250, "SEE SERIAL LOG", 2, 0xFFF4F4F4, true, 8.5f);
}

void game_draw() {
    switch (g_mode) {
    case GameMode::Field:
    case GameMode::ToBattle: field_draw(g_party.hero_hp, g_party.hero_hp_max); break;
    case GameMode::Battle:
    case GameMode::ToField:  battle_draw(); break;
    case GameMode::GameOver: draw_game_over(); break;
    case GameMode::Error:    draw_error(); break;
    }
    int a = game_fade_alpha();
    if (a > 0) eng::gfx_rect_tr(0, 0, 640, 480, (uint32_t)a << 24, 9.0f);
}

} // namespace bt
