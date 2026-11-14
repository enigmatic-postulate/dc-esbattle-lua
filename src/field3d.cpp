// field3d.cpp - see field3d.h
#include "field3d.h"
#include "assets.h"
#include "dmf.h"
#include "field.h"
#include "font.h"
#include "render3d.h"
#include <stdio.h>
#include <string.h>

namespace bt {

using namespace r3d;

// ---- tuning (3D slice 1; expect these to move in the next style passes) ----------------
// Plain variables so the host harness can try values without a rebuild (field3d_tune,
// trace_main --tune name=value). The Dreamcast build uses these defaults.
struct Tune { const char* name; float* value; };
static float CAM_FOV_Y     = 40.0f;   // degrees
static float CAM_PITCH     = 45.0f;   // degrees below horizontal
static float CAM_DIST      = 10.0f;   // units from the look-at point
static float CAM_LOOK_H    = 0.85f;   // look-at height above the hero's feet
static float CAM_FOLLOW    = 0.12f;   // fraction of the remaining offset closed per tick
static float HERO_SCALE    = 0.8f;    // x KayKit units: the Knight is ~1.9 tiles tall. At 1.0 (2.35 tiles)
                                      // his head hid foes ~3 tiles ahead when walking north.
static float TURN_RATE     = 0.30f;   // fraction of the remaining turn per tick...
static float TURN_MAX      = 0.40f;   // ...capped (radians per tick)
static float WALK_IN       = 0.20f;   // idle -> walk blend per tick
static float WALK_OUT      = 0.12f;   // walk -> idle blend per tick
static float WALK_MAX_RATE = 2.3f;    // cadence cap (x clip speed); above it the feet slide a little
                                      // rather than the legs blurring. Full speed needs ~2.7x.
static float SHADOW_ALPHA  = 120.0f;  // blob shadow darkness at the centre (0..255)
static float SLIME_SIZE    = 0.62f;   // placeholder sizes (units)
static float SPHERE_SIZE   = 0.80f;
static const Tune TUNES[] = {
    { "cam_fov", &CAM_FOV_Y }, { "cam_pitch", &CAM_PITCH }, { "cam_dist", &CAM_DIST },
    { "cam_look_h", &CAM_LOOK_H }, { "cam_follow", &CAM_FOLLOW }, { "hero_scale", &HERO_SCALE },
    { "turn_rate", &TURN_RATE }, { "turn_max", &TURN_MAX }, { "walk_in", &WALK_IN }, { "walk_out", &WALK_OUT },
    { "walk_max_rate", &WALK_MAX_RATE },
    { "shadow_alpha", &SHADOW_ALPHA }, { "slime_size", &SLIME_SIZE }, { "sphere_size", &SPHERE_SIZE },
};
const int   BORDER_TILES  = 10;      // ground drawn around the map so the view never shows a void
const float GROUND_FAR    = 40.0f;   // ground is not drawn beyond this distance from the eye
const uint32_t CLEAR_RGB  = 0x1A2B22;   // dark green-black behind the ground's far edge

static dmf::Model g_knight, g_cube, g_sphere;
static bool g_knight_ok = false, g_cube_ok = false, g_sphere_ok = false, g_inited = false;
static ModelMats g_knight_mats, g_flat_mats;
static eng::Material g_ground_mat = 0, g_shadow_mat = 0;
static int g_anim_idle = -1, g_anim_walk = -1;
static float g_stride = 1.0f;          // units per walk cycle at scale 1 (clip ground speed x length)
static float g_walk_len = 1.0f;        // seconds per walk cycle at 1x

struct View {
    Vec3  focus;                       // camera follows this point on the ground
    float yaw;                         // hero facing (0 = +Z, towards the camera)
    float walk_phase;                  // 0..1 through the walk cycle, advanced by distance
    float walk_w;                      // 0 = idle pose, 1 = walk pose
    float idle_t;                      // seconds
    float prev_x, prev_z;
    float ent_yaw[MAX_ENTITIES];
};
static View g_v;

// ---------------------------------------------------------------------------- helpers
static void hero_world(const Hero& h, float* x, float* z) {
    // centre of the 16x12 feet box (HERO_BOX_*), 1/16 px -> tiles
    *x = (float)(h.x + (HERO_BOX_X0 + HERO_BOX_X1) / 2 * SUBPX) / (float)(TILE_PX * SUBPX);
    *z = (float)(h.y + (HERO_BOX_Y0 + HERO_BOX_Y1) / 2 * SUBPX) / (float)(TILE_PX * SUBPX);
}

static void entity_world(int i, float* x, float* z) {
    int px, py;
    field_entity_pixel(i, &px, &py);
    *x = (float)(px + (ENT_BOX_X0 + ENT_BOX_X1) / 2) / (float)TILE_PX;
    *z = (float)(py + (ENT_BOX_Y0 + ENT_BOX_Y1) / 2) / (float)TILE_PX;
}

static float dir_yaw(Dir d) {
    switch (d) {
    case DIR_DOWN:  return 0.0f;
    case DIR_UP:    return 3.14159265f;
    case DIR_LEFT:  return -1.57079633f;
    case DIR_RIGHT: return 1.57079633f;
    }
    return 0.0f;
}

static float approach(float v, float target, float step) {
    return v < target ? (v + step > target ? target : v + step) : (v - step < target ? target : v - step);
}

// ---------------------------------------------------------------------------- lifecycle
void field3d_init() {
    if (g_inited) return;
    g_inited = true;
    eng::set_clear_color(CLEAR_RGB);
    g_knight_ok = dmf::load("/rd/models/knight.dmf", &g_knight);
    g_cube_ok = dmf::load("/rd/models/cube.dmf", &g_cube);
    g_sphere_ok = dmf::load("/rd/models/sphere.dmf", &g_sphere);
    // Culling stays off in this slice: every mesh is closed or double-sided, so the picture is
    // identical, and it removes the one unverified PVR setting from the first hardware run.
    // Flip to MAT_CULL after checking render3d.h's winding note in Flycast.
    const int cull = 0;
    if (g_knight_ok) {
        g_knight_mats.single = eng::material_create(g_knight.texture, eng::MAT_BILINEAR | cull);
        g_knight_mats.doubled = eng::material_create(g_knight.texture, eng::MAT_BILINEAR);
        g_anim_idle = dmf::anim_find(g_knight, "idle");
        g_anim_walk = dmf::anim_find(g_knight, "walk");
        if (g_anim_walk >= 0) {
            g_walk_len = dmf::anim_duration(g_knight, g_anim_walk);
            float gs = g_knight.anims[g_anim_walk].ground_speed;
            if (gs > 0.0f) g_stride = gs * g_walk_len;
        }
    }
    g_flat_mats.single = eng::material_create(0, cull);
    g_flat_mats.doubled = eng::material_create(0, 0);
    g_ground_mat = eng::material_create(asset_sprite("/rd/tiles.png", false), 0);   // nearest: crisp pixel art
    g_shadow_mat = eng::material_create(0, eng::MAT_TRANSLUCENT);
}

void field3d_reset() {
    const FieldState& f = field_state();
    float hx, hz;
    hero_world(f.hero, &hx, &hz);
    memset(&g_v, 0, sizeof g_v);
    g_v.focus = v3(hx, 0, hz);
    g_v.yaw = dir_yaw(f.hero.dir);
    g_v.prev_x = hx; g_v.prev_z = hz;
    for (int i = 0; i < f.ents.size(); ++i) g_v.ent_yaw[i] = dir_yaw(f.ents[i].w.dir);
}

void field3d_tick() {
    const FieldState& f = field_state();
    const Hero& h = f.hero;
    float hx, hz;
    hero_world(h, &hx, &hz);
    float dx = hx - g_v.prev_x, dz = hz - g_v.prev_z;
    float d = sqrtf(dx * dx + dz * dz);
    if (d > 1.0f) d = 0.0f;                                   // teleport (reset): no stride
    g_v.prev_x = hx; g_v.prev_z = hz;
    // walk phase follows distance (feet planted at any stick tilt), up to the cadence cap
    const float max_step = WALK_MAX_RATE / (60.0f * g_walk_len);
    const float step = d / (g_stride * HERO_SCALE);
    g_v.walk_phase += step < max_step ? step : max_step;
    g_v.walk_phase -= floorf(g_v.walk_phase);
    g_v.walk_w = approach(g_v.walk_w, d > 1e-4f ? 1.0f : 0.0f, d > 1e-4f ? WALK_IN : WALK_OUT);
    g_v.idle_t += 1.0f / 60.0f;
    if (g_v.idle_t > 3600.0f) g_v.idle_t -= 3600.0f;
    if (h.vx || h.vy) {                                       // face the stick (also against walls)
        float turn = angle_diff(g_v.yaw, atan2f((float)h.vx, (float)h.vy)) * TURN_RATE;
        if (turn > TURN_MAX) turn = TURN_MAX;
        if (turn < -TURN_MAX) turn = -TURN_MAX;
        g_v.yaw += turn;
        if (g_v.yaw > 3.14159265f) g_v.yaw -= 6.28318531f;
        if (g_v.yaw < -3.14159265f) g_v.yaw += 6.28318531f;
    }
    Vec3 want = v3(hx, 0, hz);
    if (sqrtf(dot(want - g_v.focus, want - g_v.focus)) > 8.0f) g_v.focus = want;
    g_v.focus = lerp(g_v.focus, want, CAM_FOLLOW);
    for (int i = 0; i < f.ents.size(); ++i)
        g_v.ent_yaw[i] += angle_diff(g_v.ent_yaw[i], dir_yaw(f.ents[i].w.dir)) * 0.2f;
}

bool field3d_tune(const char* name, float value) {
    for (const Tune& t : TUNES)
        if (strcmp(t.name, name) == 0) { *t.value = value; return true; }
    return false;
}

Field3dInfo field3d_info() {
    Field3dInfo i = { g_knight_ok, g_cube_ok, g_sphere_ok, g_v.focus.x, g_v.focus.z, g_v.yaw, g_v.walk_w };
    return i;
}

// ---------------------------------------------------------------------------- ground
static void ground_bounds(const Camera& cam, float* x0, float* z0, float* x1, float* z1) {
    const Vec3 r = v3(cam.view.m[0][0], cam.view.m[0][1], cam.view.m[0][2]);
    const Vec3 u = v3(cam.view.m[1][0], cam.view.m[1][1], cam.view.m[1][2]);
    const Vec3 fwd = v3(-cam.view.m[2][0], -cam.view.m[2][1], -cam.view.m[2][2]);
    *x0 = *z0 = 1e9f; *x1 = *z1 = -1e9f;
    for (int k = 0; k < 4; ++k) {
        const float sx = (k & 1) ? 1.0f : -1.0f, sy = (k & 2) ? 1.0f : -1.0f;
        Vec3 d = fwd + r * (sx * cam.tan_x) + u * (sy * cam.tan_y);
        Vec3 p;
        if (d.y < -1e-4f) p = cam.eye + d * (-cam.eye.y / d.y);
        else p = cam.eye + normalize(v3(d.x, 0, d.z)) * GROUND_FAR;      // horizon in view
        float hx = p.x - cam.eye.x, hz = p.z - cam.eye.z, hd = sqrtf(hx * hx + hz * hz);
        if (hd > GROUND_FAR) { p.x = cam.eye.x + hx * GROUND_FAR / hd; p.z = cam.eye.z + hz * GROUND_FAR / hd; }
        if (p.x < *x0) *x0 = p.x;
        if (p.x > *x1) *x1 = p.x;
        if (p.z < *z0) *z0 = p.z;
        if (p.z > *z1) *z1 = p.z;
    }
}

// Distance darkening of the ground (cheap depth cue), as a packed grey; k dims the outer border.
static uint32_t ground_shade(float dist, float k) {
    float c = 1.0f - (dist - 13.0f) * 0.022f;
    c = (c > 1.0f ? 1.0f : (c < 0.55f ? 0.55f : c)) * k;
    uint32_t v = (uint32_t)(c * 255.0f);
    return 0xFF000000u | (v << 16) | (v << 8) | v;
}

// The ground is a grid of tile quads. Grid points are transformed once (two rows of them are
// live at a time) and shared by the four quads around them; quads fully in front of the near
// plane go out on a fast path, the rest through the general clipper (r3d::emit_poly).
const int MAX_GRID_W = MAX_MAP_W + 2 * BORDER_TILES + 2;
struct GridPt { float x, y, z; uint32_t argb; bool front; Vec3 p; };

static void ground_row(const Camera& cam, int z, int x0, int x1, GridPt* row) {
    const Mat34& P = cam.proj_view;
    for (int x = x0; x <= x1; ++x) {
        GridPt& g = row[x - x0];
        g.p = v3((float)x, 0.0f, (float)z);
        const float X = P.m[0][0] * g.p.x + P.m[0][2] * g.p.z + P.m[0][3];
        const float Y = P.m[1][0] * g.p.x + P.m[1][2] * g.p.z + P.m[1][3];
        const float W = P.m[2][0] * g.p.x + P.m[2][2] * g.p.z + P.m[2][3];
        g.front = W >= cam.near_w;
        const float inv = 1.0f / (g.front ? W : cam.near_w);
        g.x = cam.cx + X * inv; g.y = cam.cy + Y * inv; g.z = inv;
        const float dx = g.p.x - cam.eye.x, dz = g.p.z - cam.eye.z;
        g.argb = ground_shade(sqrtf(dx * dx + cam.eye.y * cam.eye.y + dz * dz), 1.0f);
    }
}

// Outer border: each channel x 0.625 (= 1/2 + 1/8). The masks drop the bits a shift would move
// into the next channel down; per-channel sums stay <= 158, so the add never carries across.
static inline uint32_t dim(uint32_t argb, bool inside) {
    return inside ? argb : 0xFF000000u | (((argb & 0xFEFEFEu) >> 1) + ((argb & 0xF8F8F8u) >> 3));
}

static void draw_ground(const Camera& cam) {
    const FieldState& f = field_state();
    float fx0, fz0, fx1, fz1;
    ground_bounds(cam, &fx0, &fz0, &fx1, &fz1);
    int x0 = (int)floorf(fx0), z0 = (int)floorf(fz0), x1 = (int)ceilf(fx1), z1 = (int)ceilf(fz1);
    if (x0 < -BORDER_TILES) x0 = -BORDER_TILES;
    if (z0 < -BORDER_TILES) z0 = -BORDER_TILES;
    if (x1 > f.w + BORDER_TILES) x1 = f.w + BORDER_TILES;
    if (z1 > f.h + BORDER_TILES) z1 = f.h + BORDER_TILES;
    if (x1 <= x0 || z1 <= z0 || x1 - x0 + 1 > MAX_GRID_W) return;
    eng::Vert3* out = eng::vtx_begin((x1 - x0) * (z1 - z0) * 5);   // a near-clipped quad has up to 5 corners
    if (!out) return;
    static GridPt rows[2][MAX_GRID_W];
    GridPt* top = rows[0];
    GridPt* bot = rows[1];
    ground_row(cam, z0, x0, x1, top);
    const bool water_alt = (f.ticks / 30) % 2 == 1;
    const float CELL = 0.25f, INSET = 0.5f / 128.0f;         // tiles.png: 4x4 cells of 32x32 texels
    int used = 0;
    for (int z = z0; z < z1; ++z) {
        ground_row(cam, z + 1, x0, x1, bot);
        for (int x = x0; x < x1; ++x) {
            const GridPt& a = top[x - x0];       // (x,   z)
            const GridPt& b = bot[x - x0];       // (x,   z+1)
            const GridPt& c = bot[x + 1 - x0];   // (x+1, z+1)
            const GridPt& d = top[x + 1 - x0];   // (x+1, z)
            const bool all_front = a.front && b.front && c.front && d.front;
            if (all_front) {                     // off-screen? (the bounds are an AABB of a trapezoid)
                // plain compares: on the SH4, fminf/fmaxf are NaN-aware library calls (~200 instr each)
                if ((a.x < 0.0f && b.x < 0.0f && c.x < 0.0f && d.x < 0.0f) ||
                    (a.x > 640.0f && b.x > 640.0f && c.x > 640.0f && d.x > 640.0f) ||
                    (a.y < 0.0f && b.y < 0.0f && c.y < 0.0f && d.y < 0.0f) ||
                    (a.y > 480.0f && b.y > 480.0f && c.y > 480.0f && d.y > 480.0f)) continue;
            } else if (!a.front && !b.front && !c.front && !d.front) {
                continue;
            }
            const bool inside = x >= 0 && z >= 0 && x < f.w && z < f.h;
            int t = inside ? (int)f.tiles[z * f.w + x] : (int)TILE_GRASS;
            if (t == TILE_WATER && water_alt) t = TILE_WATER2;
            const float u0 = (float)(t % 4) * CELL + INSET, v0 = (float)(t / 4) * CELL + INSET;
            const float u1 = u0 + CELL - 2 * INSET, v1 = v0 + CELL - 2 * INSET;
            if (all_front) {
                // counter-clockwise seen from above: a(x,z) b(x,z+1) c(x+1,z+1) d(x+1,z); strip a, b, d, c
                const GridPt* q[4] = { &a, &b, &d, &c };
                const float qu[4] = { u0, u0, u1, u1 }, qv[4] = { v0, v1, v0, v1 };
                for (int k = 0; k < 4; ++k) {
                    eng::Vert3& o = out[used + k];
                    o.flags = k == 3 ? eng::VERT_EOL : eng::VERT;
                    o.x = q[k]->x; o.y = q[k]->y; o.z = q[k]->z;
                    o.u = qu[k]; o.v = qv[k];
                    o.argb = dim(q[k]->argb, inside); o.oargb = 0;
                }
                used += 4;
            } else {
                PolyVert pq[4] = { { a.p, u0, v0, dim(a.argb, inside) }, { b.p, u0, v1, dim(b.argb, inside) },
                                   { c.p, u1, v1, dim(c.argb, inside) }, { d.p, u1, v0, dim(d.argb, inside) } };
                used += emit_poly(cam, pq, 4, out + used);
            }
        }
        GridPt* tmp = top; top = bot; bot = tmp;
    }
    eng::vtx_end(g_ground_mat, used);
}

// ---------------------------------------------------------------------------- draw
void field3d_draw(int hero_hp, int hero_hp_max) {
    const FieldState& f = field_state();
    stats_reset();
    Camera cam;
    const float pitch = CAM_PITCH * 3.14159265f / 180.0f;
    Vec3 look = g_v.focus + v3(0, CAM_LOOK_H, 0);
    camera_look(&cam, look + v3(0, CAM_DIST * sinf(pitch), CAM_DIST * cosf(pitch)), look, CAM_FOV_Y);
    const Light light = default_light();

    draw_ground(cam);

    // entities: slime -> cube, everything else (the guard) -> sphere
    for (int i = 0; i < f.ents.size(); ++i) {
        const Entity& e = f.ents[i];
        if (!e.alive) continue;
        float x, z;
        entity_world(i, &x, &z);
        const float ph = (float)(f.ticks + i * 11) * 0.105f;       // ~1 s idle cycle, offset per entity
        if (e.sheet == SHEET_SLIME) {
            if (!g_cube_ok) continue;
            const float squash = sinf(ph);
            const float s = SLIME_SIZE;
            Mat34 M = mat_trs_y(v3(x, 0, z), g_v.ent_yaw[i], s * (1.0f + 0.06f * squash), s * (1.0f - 0.10f * squash),
                                s * (1.0f + 0.06f * squash));
            draw_model(cam, light, g_cube, g_flat_mats, M, nullptr);
            draw_blob_shadow(cam, g_shadow_mat, v3(x, 0, z), 0.50f, (int)(SHADOW_ALPHA * 0.9f));
        } else {
            if (!g_sphere_ok) continue;
            const float bob = 0.10f + 0.08f * sinf(ph * 0.8f);
            Mat34 M = mat_trs_y(v3(x, bob, z), g_v.ent_yaw[i], SPHERE_SIZE, SPHERE_SIZE, SPHERE_SIZE);
            draw_model(cam, light, g_sphere, g_flat_mats, M, nullptr);
            draw_blob_shadow(cam, g_shadow_mat, v3(x, 0, z), 0.42f - 0.5f * (bob - 0.10f), (int)(SHADOW_ALPHA * 0.8f));
        }
    }

    // hero
    float hx, hz;
    hero_world(f.hero, &hx, &hz);
    if (g_knight_ok) {
        static dmf::Pose pose, walk;                              // static: 1 KiB each, keep it off the stack
        static Mat34 skin[dmf::MAX_BONES];
        dmf::pose_sample(g_knight, g_anim_idle, g_v.idle_t, &pose);
        if (g_v.walk_w > 0.0f && g_anim_walk >= 0) {
            dmf::pose_sample(g_knight, g_anim_walk, g_v.walk_phase * g_walk_len, &walk);
            dmf::pose_blend(&pose, walk, g_knight.h->num_bones, g_v.walk_w);
        }
        dmf::pose_skin(g_knight, pose, skin);
        Mat34 M = mat_trs_y(v3(hx, 0, hz), g_v.yaw, HERO_SCALE, HERO_SCALE, HERO_SCALE);
        draw_model(cam, light, g_knight, g_knight_mats, M, skin);
    }
    draw_blob_shadow(cam, g_shadow_mat, v3(hx, 0, hz), 0.62f * HERO_SCALE, (int)SHADOW_ALPHA);

    // HUD: hero HP (top right)
    char hud[24];
    snprintf(hud, sizeof hud, "HP %d/%d", hero_hp, hero_hp_max);
    eng::gfx_rect_tr(500, 12, 128, 28, 0xA0000000, 7.0f);
    font_draw(510, 18, hud, 2, hero_hp * 4 > hero_hp_max ? 0xFFF4F4F4 : 0xFFEF7D57, true, 7.5f);
    if (!g_knight_ok) font_draw(20, 440, "HERO MODEL MISSING  SEE SERIAL LOG", 2, 0xFFFFCD75, true, 7.5f);
}

} // namespace bt
