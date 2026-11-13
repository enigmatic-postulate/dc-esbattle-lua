// engine.h - the seam between game code (src/) and a platform. Implementations:
//   platform/kos/main.cpp         - Dreamcast (PVR, maple input, romdisk)
//   platform/host/host_engine.cpp - host trace recorder used by the harness
//
// Draw-order contract (both backends):
//   * Opaque sprites (loaded with alpha=false) go to the OP list, alpha sprites to the TR list.
//   * Every sprite draw gets depth z = 1 + seq * 1e-4, seq = call order within the frame,
//     so later calls are in front (painter's order), unless an explicit z > 0 is passed.
//   * Rects carry their own z (OP default 1.0, TR default 0.1). Larger z = nearer.
#pragma once
#include <stdint.h>

namespace eng {

typedef int Sprite;   // 0 = invalid
const int MAX_SPRITES = 32;

Sprite sprite_load(const char* path, bool alpha = false);
void   sprite_draw(Sprite s, float cx, float cy, float w, float h, float angle, int filter = 0);
// Sample texture sub-rect [u0,u1]x[v0,v1]; argb modulates the texel (tint + alpha on TR sprites).
void   sprite_draw_uv(Sprite s, float cx, float cy, float w, float h, float angle,
                      float u0, float v0, float u1, float v1, int filter = 0,
                      uint32_t argb = 0xFFFFFFFFu, float z = 0.0f);

void gfx_rect(float x, float y, float w, float h, uint32_t argb, float z = 1.0f);
void gfx_rect_tr(float x, float y, float w, float h, uint32_t argb, float z = 0.1f);

enum Key { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_FIRE, KEY_B, KEY_Y, KEY_START, KEY_COUNT };
bool input_down(Key k);   // raw held state, active-high

float random01();
void  dbg_print(const char* s);

// Reads a whole file (e.g. "/rd/maps/test_field.txt") into buf, NUL-terminated.
// Returns the byte count, or -1 if missing / larger than cap-1.
int file_read(const char* path, char* buf, int cap);

} // namespace eng
