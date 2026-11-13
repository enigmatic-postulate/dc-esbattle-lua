// hpbar.cpp - port of battle/hpbar.lua
#include "hpbar.h"
#include "assets.h"
#include "presentation.h"

namespace bt {

static eng::Sprite g_bar_spr = 0;

void hpbar_init() { g_bar_spr = asset_sprite("/rd/plasma.png"); }

static uint32_t status_pip_color(StatusId id) {
    switch (id) {
    case StatusId::Burn:  return 0xFFFFA040;   // orange
    case StatusId::Guard: return 0xFF40A0FF;   // blue
    case StatusId::Stun:  return 0xFFFFFF80;   // pale yellow
    default:              return 0xFFC0C0C0;
    }
}

static void draw_status_pips(const Actor& a, real x, real y) {
    const int count = a.status.size();
    if (count == 0) return;

    const real pip_sz = 8, gap = 3;
    const int  max_pips = 3;
    const float z = 2.0f;
    const int  n = minv(count, max_pips);

    real px = x + 2;
    real py = y - (pip_sz + 3);

    real plate_w = (n * pip_sz) + ((n - 1) * gap) + 4 + ((count > max_pips) ? 6 : 0);
    real plate_h = pip_sz + 4;
    eng::gfx_rect_tr((float)(px - 2), (float)(py - 2), (float)plate_w, (float)plate_h, 0xC0000000, z);

    for (int i = 0; i < n; ++i) {
        uint32_t col = status_pip_color(a.status[i].id);
        eng::gfx_rect_tr((float)(px - 1), (float)(py - 1), (float)(pip_sz + 2), (float)(pip_sz + 2), 0xFF000000, z);
        eng::gfx_rect_tr((float)px, (float)py, (float)pip_sz, (float)pip_sz, col, z);
        px = px + pip_sz + gap;
    }

    if (count > max_pips) {   // overflow marker: 3 tiny dots
        eng::gfx_rect_tr((float)(px + 1), (float)(py + 3), 2, 2, 0xFFFFFFFF, z);
        eng::gfx_rect_tr((float)(px + 4), (float)(py + 3), 2, 2, 0xFFFFFFFF, z);
        eng::gfx_rect_tr((float)(px + 7), (float)(py + 3), 2, 2, 0xFFFFFFFF, z);
    }
}

static void draw_for_actor(const BattleCtx& ctx, const Actor& a) {
    if (a.hp <= 0 || a.hp_max <= 0) return;

    real ratio = clampv((real)a.hp / (real)a.hp_max, (real)0, (real)1);
    real bar_w = a.w * (real)0.90;
    real bar_h = 10;
    real w = maxv((real)2, bar_w * ratio);

    real ox, oy;
    pres_offset(ctx, &a, ox, oy);
    real x = a.x + ox;
    real y = a.y + oy - (a.h * (real)0.65);

    eng::sprite_draw(g_bar_spr, (float)x, (float)y, (float)w, (float)bar_h, 0);
    draw_status_pips(a, x, y);
}

void hpbar_draw_all(BattleCtx& ctx) {
    for (const Actor& a : ctx.actors) draw_for_actor(ctx, a);
}

} // namespace bt
