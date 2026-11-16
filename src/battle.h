// battle.h - port of battle/battle.lua, made re-entrant (slice task A4).
#pragma once
#include "battle_state.h"
#include "encounters.h"
#include "party.h"
namespace bt {
// How the battle is drawn: the 3D arena (slice 3D-4, default) or the original sprite battle (kept
// for A/B comparisons and the turn-level baselines). Set before battle_begin.
enum class BattleView : uint8_t { Sprites2D, Models3D };
void battle_set_view(BattleView v);
BattleView battle_view();
void battle_init();                                              // one-time setup
void battle_begin(const EncounterDef& enc, const PartyState& party);
void battle_update();                                            // one fixed tick; input_update() is done by the caller
void battle_draw();
BattleOutcome battle_outcome();                                  // None until the result banner is confirmed with A
int  battle_hero_hp();
int  battle_potions();                                           // the hero's potions left (back to the party)
BattleCtx& battle_ctx();                                         // read access for tools/tests
const char* battle_state_name(BattleState s);
}
