// presentation.cpp - port of battle/presentation.lua
#include "presentation.h"
#include "fastmath.h"
#include "icons.h"

namespace bt {

void pres_init(BattleCtx& ctx) {
    ctx.pres.moves.clear();
    ctx.pres.flashes.clear();
}

static void push_move(BattleCtx& ctx, Actor* a, int dur, real nx, real ny, real dist) {
    ctx.pres.moves.push(PresMove{ a, 0, dur, nx, ny, dist });
}

void pres_lunge(BattleCtx& ctx, Actor* attacker, Actor* target, int dur, real dist) {
    if (!attacker || !target) return;
    real dx = target->x - attacker->x;
    real dy = target->y - attacker->y;
    real len = rsqrt(dx * dx + dy * dy);
    real nx = 0, ny = 0;
    if (!(len < (real)0.0001)) { nx = dx / len; ny = dy / len; }
    push_move(ctx, attacker, dur, nx, ny, dist);
}

void pres_recoil(BattleCtx& ctx, Actor* target, int dur, real dist) {
    if (!target) return;
    push_move(ctx, target, dur, -1, 0, dist);   // LEGACY: always bumps left, whichever side the target is on
}

void pres_hit_flash(BattleCtx& ctx, Actor* target, int dur) {
    if (!target) return;
    ctx.pres.flashes.push(PresFlash{ target, 0, dur });
}

void pres_update(BattleCtx& ctx) {
    auto& m = ctx.pres.moves;
    for (int i = 0; i < m.size();) {
        m[i].t = m[i].t + 1;
        if (m[i].t >= m[i].dur) m.swap_remove(i); else ++i;
    }
    auto& f = ctx.pres.flashes;
    for (int j = 0; j < f.size();) {
        f[j].t = f[j].t + 1;
        if (f[j].t >= f[j].dur) f.swap_remove(j); else ++j;
    }
}

void pres_offset(const BattleCtx& ctx, const Actor* a, real& ox, real& oy) {
    ox = 0; oy = 0;
    for (const PresMove& e : ctx.pres.moves) {
        if (e.actor != a) continue;
        real u = (real)e.t / (real)e.dur;
        real k = (u < (real)0.5) ? (u / (real)0.5) : ((1 - u) / (real)0.5);   // triangle 0->1->0
        ox = ox + (e.nx * e.dist * k);
        oy = oy + (e.ny * e.dist * k);
    }
}

bool pres_done(const BattleCtx& ctx) { return ctx.pres.moves.empty() && ctx.pres.flashes.empty(); }

void pres_draw(BattleCtx& ctx) {
    for (const PresFlash& f : ctx.pres.flashes) {
        const Actor* a = f.actor;
        if (a) icon_draw(ICON_SPARK, a->x, a->y - (a->h * (real)0.75), ICON_2X);
    }
}

} // namespace bt
