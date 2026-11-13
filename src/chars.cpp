// chars.cpp - see chars.h
#include "chars.h"
#include "assets.h"
#include <string.h>

namespace bt {

static eng::Sprite g_sheets[SHEET_COUNT];

void chars_init() {
    g_sheets[SHEET_HERO]  = asset_sprite("/rd/hero.png", true);
    g_sheets[SHEET_GUARD] = asset_sprite("/rd/guard.png", true);
    g_sheets[SHEET_SLIME] = asset_sprite("/rd/slime.png", true);
}

void char_draw(CharSheet sheet, int row, int col, real cx, real cy, real size, real angle, int filter) {
    if (sheet >= SHEET_COUNT) return;
    if (sheet == SHEET_SLIME) row = 0;
    float u0 = (float)col * 0.25f, v0 = (float)row * 0.25f;
    eng::sprite_draw_uv(g_sheets[sheet], (float)cx, (float)cy, (float)size, (float)size, (float)angle,
                        u0, v0, u0 + 0.25f, v0 + 0.25f, filter);
}

CharSheet sheet_by_name(const char* name) {
    if (strcmp(name, "hero") == 0)  return SHEET_HERO;
    if (strcmp(name, "guard") == 0) return SHEET_GUARD;
    return SHEET_SLIME;
}

} // namespace bt
