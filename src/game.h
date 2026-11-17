// game.h - top-level mode manager (slice task A3): Field <-> Battle with fades, Game Over; since slice
// 3D-5 also Dialog (talk to an NPC, the after_battle script), the quest's Progress and the RAM
// checkpoints (a lost battle retries from the last one).
#pragma once
#include "party.h"
namespace bt {

enum class GameMode : uint8_t { Field, ToBattle, Battle, ToField, GameOver, Error, Dialog };

const int T_FADE_OUT = 24;   // 0.4 s
const int T_FADE_IN  = 12;   // 0.2 s

bool game_init(const char* map_path);                  // boot into the field; false if the map or its scripts failed to load
// harness: start directly in a battle, never leave it. Every skill learned; the encounter's test party
// (the boss: max HP 30, 3 potions) - the balance tests' setup. skills: override (-1: every skill).
void game_init_battle_test(const char* encounter_id, int skills = -1);
void game_tick();                                      // one fixed 60 Hz tick (samples input first)
void game_draw();

GameMode    game_mode();
const char* game_mode_name();
const PartyState& game_party();
int         game_fade_alpha();                         // 0..255 overlay currently drawn
int         game_pending_entity();                     // entity that started the current battle (-1 if none)
const char* game_error();                              // why the Error mode (map / script), "" otherwise
int         game_retries();                            // checkpoint restores so far (traces)
// The objective as shown (the {patrols} counter expanded). "" when there is none.
void        game_objective_text(char* out, int cap);

}
