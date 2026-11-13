// hpbar.cpp - HP bar + status pips (was battle/hpbar.lua, which stretched the plasma orb).
// Drawn as translucent-list rects BELOW the actor so the space above the head is
// free for cursors, turn markers and floating numbers. Layers use increasing z
// (larger z = nearer on PVR) so the fill never depends on same-depth sort order.
#include "hpbar.h"
#include "presentation.h"

namespace bt {

static const float Z_BACK = 2.00f, Z_MID = 2.01f, Z_TOP = 2.02f;
static const uint32_t COL_FRAME = 0xFF1A1C2C, COL_EMPTY = 0xFF333C57;

static void rect(real x, real y, real w, real h, uint32_t argb, float z) {
    eng::gfx_rect_tr((float)x, (float)y, (float)w, (float)h, argb, z);
}

static uint32_t status_pip_color(StatusId id) {
    switch (id) {
    case StatusId::Burn:  return 0xFFFFA040;   // orange
    case StatusId::Guard: return 0xFF40A0FF;   // blue
    case StatusId::Stun:  return 0xFFFFFF80;   // pale yellow
    default:              return 0xFFC0C0C0;
    }
}

static uint32_t fill_color(real ratio) {
    if (ratio > (real)0.5)  return 0xFF38B764;   // green
    if (ratio > (real)0.25) return 0xFFFFCD75;   // yellow
    return 0xFFB13E53;                           // red
}

static void draw_status_pips(const Actor& a, real left, real top) {
    const int count = a.status.size();
    if (count == 0) return;
    const real pip = 6, gap = 2;
    const int  max_pips = 3, n = minv(count, max_pips);
    real px = left;
    for (int i = 0; i < n; ++i) {
        rect(px - 1, top - 1, pip + 2, pip + 2, COL_FRAME, Z_BACK);
        rect(px, top, pip, pip, status_pip_color(a.status[i].id), Z_MID);
        px = px + pip + gap;
    }
    if (count > max_pips)   // overflow marker: 3 tiny dots
        for (int k = 0; k < 3; ++k) rect(px + 1 + k * 3, top + 2, 2, 2, 0xFFFFFFFF, Z_MID);
}

static void draw_for_actor(const BattleCtx& ctx, const Actor& a) {
    if (a.hp <= 0 || a.hp_max <= 0) return;

    real ratio = clampv((real)a.hp / (real)a.hp_max, (real)0, (real)1);
    real ox, oy;
    pres_offset(ctx, &a, ox, oy);

    const real bar_w = a.w * (real)0.90, bar_h = 6;
    real left = a.x + ox - bar_w * (real)0.5;
    real top  = a.y + oy + a.h * (real)0.5 + 4;

    rect(left - 1, top - 1, bar_w + 2, bar_h + 2, COL_FRAME, Z_BACK);
    rect(left, top, bar_w, bar_h, COL_EMPTY, Z_MID);
    rect(left, top, maxv((real)2, bar_w * ratio), bar_h, fill_color(ratio), Z_TOP);

    draw_status_pips(a, left, top + bar_h + 4);
}

void hpbar_draw_all(BattleCtx& ctx) {
    for (const Actor& a : ctx.actors) draw_for_actor(ctx, a);
}

} // namespace bt
