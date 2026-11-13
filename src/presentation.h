// presentation.h - port of battle/presentation.lua (lunge/recoil offsets + hit flashes).
#pragma once
#include "battle_state.h"
namespace bt {
void pres_init(BattleCtx& ctx);
void pres_lunge(BattleCtx& ctx, Actor* attacker, Actor* target, real dur = (real)0.10, real dist = 22);
void pres_recoil(BattleCtx& ctx, Actor* target, real dur = (real)0.10, real dist = 10);
void pres_hit_flash(BattleCtx& ctx, Actor* target, real dur = (real)0.12);
void pres_update(BattleCtx& ctx, real dt);
void pres_offset(const BattleCtx& ctx, const Actor* a, real& ox, real& oy);
bool pres_done(const BattleCtx& ctx);
void pres_draw(BattleCtx& ctx);
}
