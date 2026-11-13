// ui.h - port of battle/ui.lua (Mario-RPG-style command ring + target cursor).
#pragma once
#include "battle_state.h"
namespace bt {
void ui_init(BattleCtx& ctx);
void ui_reset(BattleCtx& ctx);
// Returns true and fills `out` when the player confirmed an action this frame.
bool ui_update(BattleCtx& ctx, Actor& user, PendingAction& out);
void ui_draw(BattleCtx& ctx);
}
