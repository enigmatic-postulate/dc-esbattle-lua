// engine.h - the seam that replaces the Lua globals `sprite`, `gfx`, `input`, `dbg`
// and `math.random`. Game code (src/) only talks to the platform through this
// header. Implementations:
//   platform/kos/main.cpp         - Dreamcast (PVR queues, maple input)
//   platform/host/host_engine.cpp - host trace recorder used by the harness
#pragma once
#include <stdint.h>

namespace eng {

typedef int Sprite;  // 0 = invalid (Lua: nil)

// Lua: sprite.load(path) / sprite.draw(h, x, y, w, h, angle [, filter])
Sprite sprite_load(const char* path);
void   sprite_draw(Sprite s, float cx, float cy, float w, float h, float angle, int filter = 0);
// Same, sampling only the texture sub-rectangle [u0,u1]x[v0,v1] (atlas frames).
void   sprite_draw_uv(Sprite s, float cx, float cy, float w, float h, float angle,
                      float u0, float v0, float u1, float v1, int filter = 0);

// Lua: gfx.rect / gfx.rect_tr  (z defaults match main.cpp bindings)
void gfx_rect(float x, float y, float w, float h, uint32_t argb, float z = 1.0f);
void gfx_rect_tr(float x, float y, float w, float h, uint32_t argb, float z = 0.1f);

// Lua: input.down(name). Active-high (the platform layer handles polarity).
enum Key { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_FIRE, KEY_B, KEY_Y, KEY_START, KEY_COUNT };
bool input_down(Key k);

// Lua: math.random()  -> [0,1)
float random01();

// Lua: dbg.print(s)
void dbg_print(const char* s);

} // namespace eng
