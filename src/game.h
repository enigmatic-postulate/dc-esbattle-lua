// game.h - the game flow (slice 3D-6, GDD 3 and 6; docs/NEXT_SLICE_BARROW.md 5.1): one state machine for every mode
// - the title, the new game's text card, the field (village or barrow), dialogs and cutscenes, battles, map changes,
// the death screen, the credits and the results - and one entry point for every mode change, each a catalogued
// transition (transitions.h) that locks input while it runs and needs a fresh press afterwards (GDD 4).
//
// Since slice 3D-5 also: the quest's Progress and the RAM checkpoints (a lost battle retries from the last one).
#pragma once
#include "party.h"
#include "screens.h"
namespace bt {

enum class GameMode : uint8_t { Field, ToBattle, Battle, ToField, Death, Error, Dialog, Title, Card, MapChange, Credits, Results,
                                ToTitle, Attract };

const int T_FADE_OUT = 24;   // 0.4 s (T6 out)
const int T_FADE_IN  = 12;   // 0.2 s (T6 in, and the first field frames of a harness run)

// The Dreamcast build: everything loads, then the title (T1). false if the village or the scripts failed to load.
bool game_boot();
// The host harness: straight into a new game's field on this map (no title, no intro: the `new_game` script sets the
// objective). false if the map or its scripts failed to load.
bool game_init(const char* map_path);
// harness: start directly in a battle, never leave it. Every skill learned; the encounter's test party
// (the boss: max HP 30, 3 potions) - the balance tests' setup. skills: override (-1: every skill).
void game_init_battle_test(const char* encounter_id, int skills = -1);
void game_tick();                                      // one fixed 60 Hz tick (samples input first)
void game_draw();

GameMode    game_mode();
const char* game_mode_name();
const PartyState& game_party();
int         game_fade_alpha();                         // 0..255 of the transition overlay currently drawn
int         game_pending_entity();                     // entity that started the current battle (-1 if none)
const char* game_error();                              // why the Error mode (map / script), "" otherwise
int         game_retries();                            // checkpoint restores so far (traces)
// The objective as shown (the {patrols} counter expanded). "" when there is none.
void        game_objective_text(char* out, int cap);
// ---- slice 3D-6 (traces, tests)
int         game_transition();                         // the catalogue id running (0: none)
int         game_transition_tick();                    // ticks into it
const SessionStats& game_stats();
bool        game_cleared();                            // finished once this session (the title's crest)
bool        game_skipping();                           // a cutscene skip's fast-forward is running
bool        game_attract();                            // the attract demo is playing

}
