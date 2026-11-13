// font.cpp - see font.h
#include "font.h"
#include "assets.h"

namespace bt {

static eng::Sprite g_font = 0;
static const float CELL_UV = 8.0f / 128.0f;

void font_init() { g_font = asset_sprite("/rd/font.png", true); }

real font_width(const char* s, int scale) {
    int n = 0;
    for (const char* p = s; *p; ++p) ++n;
    return (real)(n * 6 * scale);
}

static void glyphs(real x, real y, const char* s, int scale, uint32_t argb, float z) {
    const real cell = (real)(8 * scale);
    for (const char* p = s; *p; ++p, x += (real)(6 * scale)) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c >= 128) continue;
        float u0 = (float)(c % 16) * CELL_UV, v0 = (float)(c / 16) * CELL_UV;
        eng::sprite_draw_uv(g_font, (float)(x + cell * (real)0.5), (float)(y + cell * (real)0.5),
                            (float)cell, (float)cell, 0.0f, u0, v0, u0 + CELL_UV, v0 + CELL_UV, 0, argb, z);
    }
}

void font_draw(real x, real y, const char* s, int scale, uint32_t argb, bool shadow, float z) {
    if (shadow) glyphs(x + (real)scale, y + (real)scale, s, scale, argb & 0xFF000000u, z > 0 ? z - 0.001f : z);
    glyphs(x, y, s, scale, argb, z);
}

void font_draw_centered(real cx, real y, const char* s, int scale, uint32_t argb, bool shadow, float z) {
    font_draw(cx - font_width(s, scale) * (real)0.5, y, s, scale, argb, shadow, z);
}

} // namespace bt
