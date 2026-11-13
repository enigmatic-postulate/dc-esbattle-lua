// fx.h - port of battle/fx.lua (floating numbers/labels drawn as 7-segment rects).
#pragma once
#include "battle_state.h"
namespace bt {
void fx_float_text_xy(BattleCtx& ctx, real x, real y, const char* text, uint32_t color);
void fx_float_damage(BattleCtx& ctx, const Actor& a, int dmg, uint32_t color = 0xFFFFE060);
void fx_float_status(BattleCtx& ctx, const Actor& a, const char* label, uint32_t color = 0xFF80D0FF);
void fx_update(BattleCtx& ctx, real dt);
void fx_draw(BattleCtx& ctx);
}
