// transitions.h - the transition catalogue and its visuals (slice 3D-6, GDD 6; docs/NEXT_SLICE_BARROW.md 5.1).
//
// Every mode change goes through a catalogued transition: an id, its ticks and a visual. The Dreamcast's PVR has no
// post-processing, so every visual is a full-screen quad or polygon drawn last (z above the HUD), in the TR list
// unless opaque: fades (black / white), white flashes, a closing diamond iris (a 32-segment ring, opaque), a
// multiplicative tint (MAT_MULTIPLY) with a translucent overlay and a vignette (the death screen), title cards.
// Drawing only: the game's flow (game.cpp) counts the ticks.
#pragma once
#include <stdint.h>

namespace bt {

enum TransId { T_NONE, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T_COUNT };
// Ticks per phase (GDD 6): out = before the mode swaps, hold = on the swapped-to side before `in`, in = after.
struct TransDef { const char* name; int out, hold, in; const char* visual; };
const TransDef& trans_def(int id);

// Z layers (engine.h: larger is nearer). The HUD stays under 7; the dialog bars 8; transitions cover everything.
const float Z_TRANS = 9.2f, Z_TRANS_TEXT = 9.4f, Z_FADE_TOP = 9.8f;

void trans_init();                                   // the materials (once, at boot)
void trans_draw_fade(uint32_t rgb, int alpha);       // 0..255 over the whole screen
// T5: white flashes at ticks 0-3 and 4-7 (alpha from the tick), then a diamond iris closing from the screen's
// corners to the centre over `iris_ticks` (k: ticks since the flashes ended; <= 0: not started).
void trans_draw_flash_iris(int t, int iris_ticks);
// The diamond iris alone: open = 1 fully open, 0 closed (an opaque black screen).
void trans_draw_iris(float open);
// T9 / the death screen: the frame multiplied toward `tint_rgb` by w (0..1), a translucent overlay and a dark
// vignette (edges darker, alpha up to vig_alpha).
void trans_draw_tint(uint32_t tint_rgb, float w, uint32_t overlay_argb, int vig_alpha);
// A centred title card line in the dialog font (T2's text, THE END): alpha 0..255, y = its top.
void trans_draw_card_text(const char* text, float y, int alpha, uint32_t rgb, float scale_px = 1.0f);

}
