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
    MAT_ADDITIVE    = 8,   // TR list, additive blending: dst + src * src_alpha (glows, sparks, fire)
};
// texture: a Sprite handle (PNG or memory texture) or 0 for untextured (vertex colour only).
Material material_create(Sprite texture, int flags);

// Texture from memory: linear 16-bit texels, pow2 sizes. fmt: 1 RGB565, 2 ARGB4444, 3 ARGB1555
// (the .dmf codes). Returns a Sprite handle (it uses one of the MAX_SPRITES slots).
Sprite texture_create(const uint16_t* texels, int w, int h, int fmt);

// Replaces the texels of a memory texture (same size and format as at texture_create), e.g. the two
// portrait slots (slice 3D-5). KOS: uploaded into the same VRAM block at once, so the
// caller must not have drawn `s` in the last 2 frames (the PVR renders frame N-1 while tick N runs:
// src/portrait.cpp waits that long). Returns false if `s` is not a memory texture.
// twiddled = true: the texels are already in the PVR's twiddled order (KOS TWIDOUT(x, y): y's bits at the
// even positions, x's at the odd ones; square textures) and are copied as they are - no per-texel work on
// the SH4 (src/portrait.cpp twiddles its cache at load time).
bool texture_update(Sprite s, const uint16_t* texels, bool twiddled = false);
// The index of texel (x, y) of a square pow2 texture in twiddled order (the KOS TWIDOUT macro).
inline uint32_t twiddle_index(uint32_t x, uint32_t y) {
    uint32_t r = 0;
    for (int b = 0; b < 10; ++b) r |= ((y >> b) & 1u) << (2 * b) | ((x >> b) & 1u) << (2 * b + 1);
    return r;
}

// VQ texture (the PVR's compressed format, 2 bits per texel): `data` is exactly what the PVR reads - a 2 KiB
// codebook of 256 entries x 4 RGB565 texels (a 2x2 block in twiddled order: (0,0) (0,1) (1,0) (1,1)), then the
// (w/2) x (h/2) index bytes in twiddled order. Square pow2 sizes 16..1024; bytes = 2048 + w*h/4. Uploaded as
// it is (no per-texel work); the village ground (tools/ground/paint_ground.py, romdisk/maps/<map>.gnd).
// texture_update() does not apply to it. GROUND_NO_VQ builds (KOS) decode it and upload plain RGB565 instead.
const int VQ_CODEBOOK_BYTES = 2048;
Sprite texture_create_vq(const void* data, int bytes, int w, int h);
// The same decoded to linear RGB565 texels (w*h): the host harness, and the GROUND_NO_VQ fallback.
inline void vq_decode(const uint8_t* data, int w, int h, uint16_t* out) {
    const uint16_t* cb = (const uint16_t*)data;
    const uint8_t* idx = data + VQ_CODEBOOK_BYTES;
    for (int by = 0; by < h / 2; ++by)
        for (int bx = 0; bx < w / 2; ++bx) {
            const uint16_t* e = cb + 4 * idx[twiddle_index((uint32_t)bx, (uint32_t)by)];
            out[(2 * by) * w + 2 * bx] = e[0];         // (0,0)
            out[(2 * by + 1) * w + 2 * bx] = e[1];     // (0,1)
            out[(2 * by) * w + 2 * bx + 1] = e[2];     // (1,0)
            out[(2 * by + 1) * w + 2 * bx + 1] = e[3]; // (1,1)
        }
}

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
