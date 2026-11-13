// battle.h - port of battle/battle.lua, made re-entrant (slice task A4).
#pragma once
#include "battle_state.h"
#include "encounters.h"
#include "party.h"
namespace bt {
void battle_init();                                              // one-time setup
void battle_begin(const EncounterDef& enc, const PartyState& party);
void battle_update();                                            // one fixed tick; input_update() is done by the caller
void battle_draw();
BattleOutcome battle_outcome();                                  // None until the result banner is confirmed with A
int  battle_hero_hp();
BattleCtx& battle_ctx();                                         // read access for tools/tests
const char* battle_state_name(BattleState s);
}
