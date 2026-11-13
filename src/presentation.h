// presentation.h - port of battle/presentation.lua (lunge/recoil offsets + hit flashes).
#pragma once
#include "battle_state.h"
namespace bt {
// Hurt marker: drawn on the frame it is set plus 10 / 13 more (Lua 0.18 s / 0.22 s).
const int T_HURT_HIT = 11, T_HURT_AOE = 14;
void pres_init(BattleCtx& ctx);
// Durations in ticks (60/s). Calibrated equivalents of the Lua seconds:
// 0.10 s -> 6, 0.12 s -> 8, 0.16 s -> 10, 0.20 s -> 12.
const int T_MOVE = 6, T_FLASH_HIT = 8, T_FLASH_AOE = 10, T_FLASH_GUARD = 12;
void pres_lunge(BattleCtx& ctx, Actor* attacker, Actor* target, int dur = T_MOVE, real dist = 22);
void pres_recoil(BattleCtx& ctx, Actor* target, int dur = T_MOVE, real dist = 10);
void pres_hit_flash(BattleCtx& ctx, Actor* target, int dur = T_FLASH_HIT);
void pres_update(BattleCtx& ctx);
void pres_offset(const BattleCtx& ctx, const Actor* a, real& ox, real& oy);
bool pres_done(const BattleCtx& ctx);
void pres_draw(BattleCtx& ctx);
}
