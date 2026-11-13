// turn_system.h - port of battle/turn_system.lua
#pragma once
#include "battle_state.h"
namespace bt {
void   build_order(BattleCtx& ctx);                 // fills ctx.order
bool   start_turn(BattleCtx& ctx, Actor& actor);    // returns can_act
Actor* advance_turn(BattleCtx& ctx);                // next actor that can act, or nullptr
}
