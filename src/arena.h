// arena.h - battle arenas (slice 3D-4): romdisk/arenas/<name>.txt -> a prelit static scene, the
// ground (grass + a dirt clearing), formation slots and named camera shots. Format: see
// romdisk/arenas/village.txt. Loaded once per session; drawing allocates nothing.
#pragma once
#include "render3d.h"

namespace bt {

const int ARENA_MAX_PROPS = 64;
const int ARENA_MAX_SHOTS = 8;

struct ArenaShot { char name[16]; r3d::Vec3 eye, target; float fov; };

struct ArenaProp {
    int            model;          // models_get() index
    r3d::Mat34     world;
    const uint32_t* colors;        // prelit (light + tint)
    float          x, z;
};

struct Arena {
    bool       ok;
    char       name[16];
    r3d::Light light;
    float      light_rgb[3];       // light colour (multiplies the prelit scenery and the characters)
    uint32_t   light_mul[3];       // the same, 0..256 fixed point (per-frame colour work)
    uint32_t   sky, horizon;
    int        ground;             // 0 grass, 1 dirt, 2 stone (outer ground cell)
    float      clear_r, rim;       // dirt clearing: opaque inside clear_r - rim, fades out to clear_r
    ArenaProp  props[ARENA_MAX_PROPS];
    int        num_props;
    float      hero_x, hero_z;
    float      slot_x[4][4], slot_z[4][4];   // [count-1][i]
    ArenaShot  shots[ARENA_MAX_SHOTS];
    int        num_shots;
    int        prop_tris;          // total triangles of the props (before culling)
};

// Loads the arena (and its prop models). false (and a log line) on a bad file.
bool arena_load(const char* path, Arena* out);
const ArenaShot* arena_shot(const Arena& a, const char* name);
// Colour scaled by the arena light colour (0xAARRGGBB).
uint32_t arena_light_color(const Arena& a, uint32_t argb);
// Integer version (light_mul), for per-frame loops.
inline uint32_t arena_light_color_fast(const Arena& a, uint32_t c) {
    const uint32_t r = (((c >> 16) & 255u) * a.light_mul[0]) >> 8, g = (((c >> 8) & 255u) * a.light_mul[1]) >> 8;
    const uint32_t b = ((c & 255u) * a.light_mul[2]) >> 8;
    return (c & 0xFF000000u) | ((r > 255u ? 255u : r) << 16) | ((g > 255u ? 255u : g) << 8) | (b > 255u ? 255u : b);
}
void arena_draw(const Arena& a, const r3d::Camera& cam, int* ground_verts = nullptr);

}
