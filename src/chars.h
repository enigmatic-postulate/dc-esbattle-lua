// chars.h - character sprite sheets (hero / guard / slime), 128x128, 4x4 cells of 32x32
// (16x16 art at 2x). Rows = facing (down, up, left, right), cols = frames.
#pragma once
#include "common.h"
namespace bt {

enum CharSheet : uint8_t { SHEET_HERO, SHEET_GUARD, SHEET_SLIME, SHEET_COUNT };
enum Dir : uint8_t { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };
enum CharFrame : uint8_t { FRAME_STAND = 0, FRAME_WALK_A = 1, FRAME_WALK_B = 2, FRAME_EXTRA = 3 };

void chars_init();
// size: on-screen size of the 16x16 art (32 = 2x, 64 = 4x). Slime sheets have one row.
void char_draw(CharSheet sheet, int row, int col, real cx, real cy, real size, real angle = 0, int filter = 0);
CharSheet sheet_by_name(const char* name);   // "hero" / "guard" / "slime"; defaults to slime

}
