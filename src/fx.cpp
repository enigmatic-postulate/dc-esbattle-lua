// fx.cpp - port of battle/fx.lua
#include "fx.h"
#include <stdio.h>
#include <string.h>

namespace bt {

static const float FX_Z = 2.0f;

// a, b, c, d, e, f, g  (top, top-right, bottom-right, bottom, bottom-left, top-left, middle)
static const uint8_t SEG[10][7] = {
    {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1}, {0,1,1,0,0,1,1},
    {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1},
};

static uint32_t with_alpha(uint32_t argb, real a01) {
    int a = clampv((int)floor(255 * a01 + (real)0.5), 0, 255);
    return ((uint32_t)a << 24) | (argb & 0x00FFFFFFu);
}

static void rect(real x, real y, real w, real h, uint32_t argb) {
    eng::gfx_rect_tr((float)x, (float)y, (float)w, (float)h, argb, FX_Z);
}

static void draw_seg_digit(real x, real y, int digit, real s, uint32_t argb) {
    const uint8_t* on = SEG[digit];
    real w = 6 * s, h = 10 * s, t = 2 * s;
    if (on[0]) rect(x + t,     y,             w, t, argb);   // a
    if (on[1]) rect(x + w + t, y + t,         t, h, argb);   // b
    if (on[2]) rect(x + w + t, y + h + 2 * t, t, h, argb);   // c
    if (on[3]) rect(x + t,     y + 2 * h + 2 * t, w, t, argb); // d
    if (on[4]) rect(x,         y + h + 2 * t, t, h, argb);   // e
    if (on[5]) rect(x,         y + t,         t, h, argb);   // f
    if (on[6]) rect(x + t,     y + h + t,     w, t, argb);   // g
}

static void draw_sevenseg_string(real x, real y, const char* str, real s, uint32_t argb) {
    real cx = x;
    for (const char* p = str; *p; ++p) {
        char ch = *p;
        if (ch == '-') {
            rect(cx + 2 * s, y + 10 * s + 2 * s, 6 * s, 2 * s, argb);
            cx = cx + 10 * s;
        } else if (ch == ':') {
            rect(cx, y + 6 * s,  2 * s, 2 * s, argb);
            rect(cx, y + 14 * s, 2 * s, 2 * s, argb);
            cx = cx + 6 * s;
        } else if (ch >= '0' && ch <= '9') {
            draw_seg_digit(cx, y, ch - '0', s, argb);
            cx = cx + 12 * s;
        } else {
            // Unknown glyph (incl. ALL letters): tiny block. LEGACY: "BURN"/"GUARD"/"SKIP"
            // therefore render as rows of dots - there is no letter font yet.
            rect(cx, y + 10 * s, 4 * s, 4 * s, argb);
            cx = cx + 8 * s;
        }
    }
}

void fx_float_text_xy(BattleCtx& ctx, real x, real y, const char* text, uint32_t color) {
    FloatText f;
    f.x = x; f.y = y;
    f.vx = ((real)eng::random01() * 2 - 1) * 18;   // -18..+18 px/sec
    f.vy = -42;
    f.t = 0;
    f.dur = (real)0.70;
    strncpy(f.text, text ? text : "", sizeof f.text - 1);
    f.text[sizeof f.text - 1] = '\0';
    f.color = color;
    f.scale = 1;
    ctx.fx.floats.push(f);   // drops when full (64 concurrent)
}

void fx_float_damage(BattleCtx& ctx, const Actor& a, int dmg, uint32_t color) {
    char buf[12];
    snprintf(buf, sizeof buf, "%d", dmg);
    fx_float_text_xy(ctx, a.x, a.y - a.h * (real)0.75, buf, color);
}

void fx_float_status(BattleCtx& ctx, const Actor& a, const char* label, uint32_t color) {
    fx_float_text_xy(ctx, a.x, a.y - a.h * (real)0.95, label, color);
}

void fx_update(BattleCtx& ctx, real dt) {
    auto& v = ctx.fx.floats;
    int i = 0;
    while (i < v.size()) {
        FloatText& f = v[i];
        f.t = f.t + dt;
        f.x = f.x + (f.vx * dt);
        f.y = f.y + (f.vy * dt);
        if (f.t >= f.dur) v.swap_remove(i);
        else ++i;
    }
}

void fx_draw(BattleCtx& ctx) {
    for (const FloatText& f : ctx.fx.floats) {
        real a01 = (real)1.0 - (f.t / f.dur);
        draw_sevenseg_string(f.x, f.y, f.text, f.scale, with_alpha(f.color, a01));
    }
}

} // namespace bt
