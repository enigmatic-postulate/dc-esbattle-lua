// icons.cpp - draws cells of the 128x128 UI atlas (4x4 grid of 32x32 cells).
#include "icons.h"
#include "assets.h"

namespace bt {

static eng::Sprite g_atlas = 0;
static const int   ATLAS_COLS = 4;
static const float CELL_UV = 0.25f;   // 32 / 128

void icons_init() { g_atlas = asset_sprite("/rd/ui_icons.png", true); }   // alpha: no black boxes

void icon_draw(Icon icon, real cx, real cy, real size) {
    if (icon < 0 || icon >= ICON_COUNT) return;
    float u0 = (float)(icon % ATLAS_COLS) * CELL_UV;
    float v0 = (float)(icon / ATLAS_COLS) * CELL_UV;
    eng::sprite_draw_uv(g_atlas, (float)cx, (float)cy, (float)size, (float)size, 0.0f,
                        u0, v0, u0 + CELL_UV, v0 + CELL_UV, 0);
}

void icon_draw_tint(Icon icon, real cx, real cy, real size, uint32_t argb, float z) {
    if (icon < 0 || icon >= ICON_COUNT) return;
    float u0 = (float)(icon % ATLAS_COLS) * CELL_UV;
    float v0 = (float)(icon / ATLAS_COLS) * CELL_UV;
    eng::sprite_draw_uv(g_atlas, (float)cx, (float)cy, (float)size, (float)size, 0.0f,
                        u0, v0, u0 + CELL_UV, v0 + CELL_UV, 0, argb, z);
}

} // namespace bt
