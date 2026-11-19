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
// Slice 3D-6: set before battle_begin (kept until changed). auto_lose: the loss is final as soon as the killing
// blow's sequence ends (the death screen shows it). auto_win: the win ends itself BOSS_WIN_HOLD ticks after the
// VICTORY banner (no "press A": the boss, GDD 14 E1).
void battle_set_auto(bool auto_lose, bool auto_win);
const int BOSS_WIN_HOLD = 150;
const BattleTally& battle_tally();                               // this battle's counts (the results card)
int  battle_hero_hp();
int  battle_potions();                                           // the hero's potions left (back to the party)
BattleCtx& battle_ctx();                                         // read access for tools/tests
const char* battle_state_name(BattleState s);
}
