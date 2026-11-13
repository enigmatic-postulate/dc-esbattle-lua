// game.h - top-level mode manager (slice task A3): Field <-> Battle with fades, Game Over.
#pragma once
#include "party.h"
namespace bt {

enum class GameMode : uint8_t { Field, ToBattle, Battle, ToField, GameOver, Error };

const int T_FADE_OUT = 24;   // 0.4 s
const int T_FADE_IN  = 12;   // 0.2 s

bool game_init(const char* map_path);                  // boot into the field; false if the map failed to load
void game_init_battle_test(const char* encounter_id);  // harness: start directly in a battle, never leave it
void game_tick();                                      // one fixed 60 Hz tick (samples input first)
void game_draw();

GameMode    game_mode();
const char* game_mode_name();
const PartyState& game_party();
int         game_fade_alpha();                         // 0..255 overlay currently drawn
int         game_pending_entity();                     // entity that started the current battle (-1 if none)

}
