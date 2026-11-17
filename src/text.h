// text.h - the dialog font (slice 3D-5, task B2; GDD 7.5): metrics, wrapping and drawing of the
// proportional pixel font built by tools/dialog/make_font.py (romdisk/fonts/dialog.fnt + dialog.tex,
// one 256x128 ARGB4444 texture: glyphs in the top half, UI icons in the bottom half).
//
// Metrics are integers (pixels) so that the script compiler's page wrapping (src/script.cpp) and the
// lint's (tools/dialog/lint.py) are identical on every platform (tests/dialog: parity check).
// Text syntax: '\n' ends a line; colour codes {gold} {red} {grey} {/} (back to the draw colour) have
// zero width and are skipped by the typewriter. The 5x7 font (font.h) stays for numbers and the HUD.
#pragma once
#include "engine.h"
#include <stdint.h>

namespace bt {

// Icon slots of the atlas's bottom half (order = ICONS in make_font.py).
enum TextIcon { TI_MARKER_QUEST, TI_MARKER_TALK, TI_EMOTE_EXCL, TI_EMOTE_QUESTION, TI_EMOTE_DOTS, TI_EMOTE_SWEAT,
                TI_EMOTE_ANGER, TI_ADVANCE, TI_CURSOR, TI_LOCK, TI_TOKEN, TI_COUNT };

const uint32_t TEXT_GOLD = 0xFFFFCD75u, TEXT_RED = 0xFFEF7D57u, TEXT_GREY = 0xFFA0A0A8u;

bool text_init();                       // loads the font once; false (and a log line) if missing/invalid
bool text_ready();
int  text_line_pitch();                 // px between line tops (22)
int  text_ink_height();                 // px of a capital (14)
int  text_advance(char c);              // px; 0 for characters the font lacks
// Width in px of s up to its end, a '\n', or n bytes (n < 0: no limit). Colour codes count 0.
int  text_width(const char* s, int n = -1);
// Visible characters of s (spaces included), excluding '\n' and colour codes: the typewriter's length.
int  text_glyph_count(const char* s);
// Word-wraps src into dst ('\n' between lines, NUL-terminated): words are separated by single spaces,
// '|' forces a break, every line <= width px. Returns the number of lines, or -1 if a word is wider than
// width, the text has a double space / an unknown colour code, or dst (cap bytes) is too small.
int  text_wrap(const char* src, int width, char* dst, int cap);
// Draws s with its top-left at (x, y) (integer pixels keep the pixel font crisp), at depth z, showing at
// most max_glyphs visible characters (< 0: all). shadow: a 2 px drop shadow at z - 0.1 first.
// alpha (0..255) scales the colour's and the shadow's alpha. Returns the number of sprites queued.
int  text_draw(float x, float y, const char* s, uint32_t argb, float z, int max_glyphs = -1, bool shadow = true,
               int alpha = 255);
// Icons of the atlas's bottom half: drawn 1:1 at (cx, cy) centre x scale, modulated by argb.
void text_icon_draw(TextIcon i, float cx, float cy, float scale, uint32_t argb, float z);
void text_icon_uv(TextIcon i, float* u0, float* v0, float* u1, float* v1);   // half-texel inset (bilinear)
void text_icon_size(TextIcon i, int* w, int* h);
eng::Sprite text_texture();             // the atlas (billboard materials: markers and emotes)

}
