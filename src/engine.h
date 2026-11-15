// engine.h - the seam between game code (src/) and a platform. Implementations:
//   platform/kos/main.cpp         - Dreamcast (PVR, maple input, romdisk)
//   platform/host/host_engine.cpp - host trace recorder used by the harness
//
// Draw-order contract (both backends):
//   * Opaque sprites (loaded with alpha=false) go to the OP list, alpha sprites to the TR list.
//   * Every sprite draw gets depth z = 1 + seq * 1e-4, seq = call order within the frame,
//     so later calls are in front (painter's order), unless an explicit z > 0 is passed.
//   * Rects carry their own z (OP default 1.0, TR default 0.1). Larger z = nearer.
//   * 3D batches (vtx_begin/vtx_end) carry per-vertex z = 1/w < 1, so they sit behind every
//     sprite and rect. Per list, 3D batches are submitted first, then sprites, then rects.
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
// Main analog stick, -128..127 per axis (0 = centred; +x right, +y down, as KOS reports it).
enum Axis { AXIS_X, AXIS_Y, AXIS_COUNT };
int  input_axis(Axis a);

// ---------------------------------------------------------------------------- 3D (field)
// Screen-space vertex, byte-for-byte the PVR TA vertex (pvr_vertex_t, 32 bytes): the game
// code writes transformed strips straight into the engine's per-frame arena and the KOS
// backend submits each batch with one header + one pvr_prim() bulk copy.
//   x, y: screen pixels (640x480, y down); z: 1/w (larger = nearer; OP depth test GREATER).
//   3D geometry stays below z = 1/near (< 1); HUD rects and sprites keep z >= 1.
struct Vert3 { uint32_t flags; float x, y, z, u, v; uint32_t argb, oargb; };
const uint32_t VERT     = 0xE0000000u;   // == PVR_CMD_VERTEX
const uint32_t VERT_EOL = 0xF0000000u;   // == PVR_CMD_VERTEX_EOL (ends a strip)

typedef int Material;    // 0 = invalid
enum MaterialFlags {
    MAT_TRANSLUCENT = 1,   // TR list (alpha from vertex colour x texel), else OP list
    MAT_BILINEAR    = 2,   // else nearest
    MAT_CULL        = 4,   // cull back faces (front = counter-clockwise on screen; see render3d.cpp)
};
// texture: a Sprite handle (PNG or memory texture) or 0 for untextured (vertex colour only).
Material material_create(Sprite texture, int flags);

// Texture from memory: linear 16-bit texels, pow2 sizes. fmt: 1 RGB565, 2 ARGB4444, 3 ARGB1555
// (the .dmf codes). Returns a Sprite handle (it uses one of the MAX_SPRITES slots).
Sprite texture_create(const uint16_t* texels, int w, int h, int fmt);

// Per-frame vertex arena. vtx_begin() reserves up to `max` vertices (nullptr if the frame's
// arena is full: the caller skips the draw); vtx_end() commits the first `used` of them as one
// batch drawn with `m`. One batch may be open at a time. The arena resets every frame.
Vert3* vtx_begin(int max);
void   vtx_end(Material m, int used);

// Background colour behind everything (0xRRGGBB).
void set_clear_color(uint32_t rgb);

float random01();
void  dbg_print(const char* s);
// Boot/progress breadcrumb: always on in the Dreamcast build (serial log, flushed at once,
// with a millisecond timestamp), silent on the host. A log that stops after a breadcrumb
// names the stage that stalled.
void  boot_log(const char* s);

// Reads a whole file (e.g. "/rd/maps/test_field.txt") into buf, NUL-terminated.
// Returns the byte count, or -1 if missing / larger than cap-1.
int file_read(const char* path, char* buf, int cap);
bool file_exists(const char* path);

} // namespace eng
