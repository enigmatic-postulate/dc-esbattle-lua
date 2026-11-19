// uitext.h - the UI font and icons (docs/UI_ART.md): romdisk/ui/ui.uia, one 512x256 ARGB4444 atlas built by
// tools/ui/make_ui_atlas.py - an anti-aliased font (Poppins Bold, SIL OFL) in two sizes with its outline and drop
// shadow baked in, and the HUD / battle icons (the sword and shield rendered from the game's own weapon models).
// Replaces, in the 3D game, the 5x7 bitmap font (font.h) and the 16x16 pixel icons (icons.h) of the 2D battle engine,
// which stay for the legacy 2D battle view only.
//
// Cost: one bilinear sprite per glyph or icon (the old outlined text drew every glyph 5 times, shadowed text twice).
// At scale 1 every glyph lands on whole pixels (crisp); other scales are smooth (bilinear), for popping numbers.
// Without the atlas (a missing file) everything falls back to the old font and icons - the game never shows nothing.
#pragma once
#include <stdint.h>

namespace bt {

enum UiFont { UF_S, UF_L };              // S: cap 14 px (the HUD); L: cap 28 px (numbers, banners, titles)
// Order == ICONS in tools/ui/make_ui_atlas.py.
enum UiIcon { UI_SWORD, UI_FLAME, UI_SHIELD, UI_POTION, UI_PAD_A, UI_PAD_B, UI_PAD_X, UI_PAD_Y, UI_PAD_START,
              UI_PAD_LEFT, UI_PAD_RIGHT, UI_PAD_UP, UI_PAD_DOWN, UI_LOCK, UI_POTION_S, UI_TOKEN, UI_ICON_COUNT };
enum UiAlign { UA_LEFT, UA_CENTRE, UA_RIGHT };

bool  ui_init();                         // at boot (game.cpp); false (and a log line) if the atlas is missing/invalid
bool  ui_ready();
int   ui_cap(UiFont f);                  // cap height in px at scale 1 (14 / 28)
float ui_text_width(UiFont f, const char* s, float scale = 1.0f);
// Draws s with its cap line (the top of the capitals) at y; x is the left edge, the centre or the right edge (al).
// Lower case is drawn with the capitals. argb modulates the fill (the outline stays dark) and the alpha.
// Returns the number of sprites queued.
int   ui_text(UiFont f, float x, float y, const char* s, uint32_t argb, float z, float scale = 1.0f, UiAlign al = UA_LEFT);
// The same at a given cap height in px, in the size that suits it (S up to 17 px, L above): the old 5x7 font at
// scale k had 7k px capitals, so ui_text_px(x, y, s, 7 * k, ...) replaces font_draw(x, y, s, k, ...).
int   ui_text_px(float x, float y, const char* s, float cap_px, uint32_t argb, float z, UiAlign al = UA_LEFT);
// An icon centred at (cx, cy), size px square (its cell: 48 px for the four battle commands, 24 for the rest).
void  ui_icon(UiIcon i, float cx, float cy, float size, uint32_t argb, float z);

}
