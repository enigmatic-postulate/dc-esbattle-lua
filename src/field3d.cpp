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

// ---- tuning (expect these to move in the next style passes) ------------------------------
// Plain variables so the host harness can try values without a rebuild (field3d_tune,
// trace_main --tune name=value). The Dreamcast build uses these defaults.
struct Tune { const char* name; float* value; };
static float CAM_FOV_Y     = 40.0f;   // degrees
static float CAM_PITCH     = 45.0f;   // degrees below horizontal
static float CAM_DIST      = 10.5f;   // units from the look-at point
static float CAM_LOOK_H    = 0.85f;   // look-at height above the hero's feet
static float CAM_FOLLOW    = 0.12f;   // fraction of the remaining offset closed per tick
static float HERO_SCALE    = 0.8f;    // x KayKit units: the Knight is ~1.9 tiles tall
static float ENEMY_SCALE   = 0.8f;
static float TURN_RATE     = 0.30f;   // fraction of the remaining turn per tick...
static float TURN_MAX      = 0.40f;   // ...capped (radians per tick)
static float MOVE_IN       = 0.20f;   // idle -> locomotion blend per tick
static float MOVE_OUT      = 0.12f;   // locomotion -> idle blend per tick
static float RUN_FROM      = 1.0f;    // tiles/s: pure walk below, pure run above RUN_FULL
static float RUN_FULL      = 1.7f;
static float MAX_RATE      = 1.6f;    // cadence cap (x clip speed); above it the feet slide a little
static float SHADOW_ALPHA  = 120.0f;  // blob shadow darkness at the centre (0..255)
static float FADE_ALPHA    = 55.0f;   // alpha of a house/tree that hides the hero (0..255); front faces only
static float FADE_RATE     = 0.12f;   // fade in/out per tick
static float HERO_DYN_LIGHT = 0.0f;   // 1 = light the hero per vertex every frame (~200k SH4
                                      // instructions); 0 = lighting baked per yaw (32 buckets, idle pose)
static const Tune TUNES[] = {
    { "cam_fov", &CAM_FOV_Y }, { "cam_pitch", &CAM_PITCH }, { "cam_dist", &CAM_DIST },
    { "cam_look_h", &CAM_LOOK_H }, { "cam_follow", &CAM_FOLLOW }, { "hero_scale", &HERO_SCALE },
    { "enemy_scale", &ENEMY_SCALE }, { "turn_rate", &TURN_RATE }, { "turn_max", &TURN_MAX },
    { "move_in", &MOVE_IN }, { "move_out", &MOVE_OUT }, { "run_from", &RUN_FROM }, { "run_full", &RUN_FULL },
    { "max_rate", &MAX_RATE }, { "shadow_alpha", &SHADOW_ALPHA }, { "fade_alpha", &FADE_ALPHA },
    { "fade_rate", &FADE_RATE }, { "hero_dyn_light", &HERO_DYN_LIGHT },
};
const int   BORDER_TILES  = 10;      // ground drawn around the map so the view never shows a void
const float GROUND_FAR    = 40.0f;   // ground is not drawn beyond this distance from the eye
const float DRAW_RANGE_X  = 15.0f;   // props farther than this from the camera focus are skipped
const float DRAW_RANGE_Z  = 18.0f;   //   before the per-instance frustum test (cheap 2D reject)
const uint32_t CLEAR_RGB  = 0x1A2B22;

// ---------------------------------------------------------------------------- models
// Every model the field can show, loaded once (romdisk/models/<name>.dmf).
const int MAX_MODELS = 24;
struct LoadedModel { char name[24]; dmf::Model m; ModelGfx gfx, faded; bool ok; };   // e.g. "house_plaster_r180" (18 chars)
static LoadedModel g_models[MAX_MODELS];
static int g_nmodels = 0;

static int model_get(const char* name) {
    for (int i = 0; i < g_nmodels; ++i)
        if (strcmp(g_models[i].name, name) == 0) return g_models[i].ok ? i : -1;
    if (g_nmodels >= MAX_MODELS) return -1;
    LoadedModel& lm = g_models[g_nmodels];
    snprintf(lm.name, sizeof lm.name, "%s", name);
    char path[48];
    snprintf(path, sizeof path, "/rd/models/%s.dmf", name);
    eng::boot_log(path);
    lm.ok = dmf::load(path, &lm.m) && gfx_init(&lm.gfx, lm.m, false);
    ++g_nmodels;
    return lm.ok ? g_nmodels - 1 : -1;
}

// Translucent copy of a model's materials (TR list), for houses/trees that hide the hero.
static void make_faded(LoadedModel& lm) {
    lm.faded = lm.gfx;
    for (int k = 0; k < lm.m.num_mats; ++k) {
        int f = eng::MAT_TRANSLUCENT;
        if (lm.m.mat_flags[k] & dmf::MAT_BILINEAR) f |= eng::MAT_BILINEAR;
        // front faces only while faded: the back walls/roof slope would stack more translucent
        // layers into the view (the house sits between the camera and the hero, i.e. close).
        lm.faded.single[k] = lm.faded.doubled[k] = eng::material_create(lm.m.mat_tex[k], f | eng::MAT_CULL);
    }
}

static int g_knight = -1, g_warrior = -1, g_mage = -1;
// Enemies only turn in yaw: their lighting is baked per yaw bucket (idle-pose normals).
const int ENEMY_YAW_BUCKETS = 16;
static uint32_t* g_enemy_lit[2][ENEMY_YAW_BUCKETS];
const int HERO_YAW_BUCKETS = 32;
static uint32_t g_hero_lit[HERO_YAW_BUCKETS][2048];
static bool g_hero_lit_ok = false;

static int yaw_bucket(float yaw, int buckets) {
    int b = (int)floorf(yaw * ((float)buckets / 6.28318531f) + 0.5f) % buckets;
    return b < 0 ? b + buckets : b;
}
static int g_fence = -1, g_tree = -1;
struct Clips { int idle, walk, run; float walk_len, run_len, walk_stride, run_stride; };
static Clips g_hero_clips, g_enemy_clips[2];
static eng::Material g_ground_mat = 0, g_shadow_mat = 0;

static Clips clips_of(int mi) {
    Clips c = { -1, -1, -1, 1.0f, 1.0f, 1.0f, 1.0f };
    if (mi < 0) return c;
    const dmf::Model& m = g_models[mi].m;
    c.idle = dmf::anim_find(m, "idle");
    c.walk = dmf::anim_find(m, "walk");
    c.run = dmf::anim_find(m, "run");
    if (c.walk >= 0) { c.walk_len = dmf::anim_duration(m, c.walk); c.walk_stride = m.anims[c.walk].ground_speed * c.walk_len; }
    if (c.run >= 0) { c.run_len = dmf::anim_duration(m, c.run); c.run_stride = m.anims[c.run].ground_speed * c.run_len; }
    if (!(c.walk_stride > 0.0f)) c.walk_stride = 1.0f;
    if (!(c.run_stride > 0.0f)) c.run_stride = c.walk_stride;
    return c;
}

// ---------------------------------------------------------------------------- static scene
// Everything that never moves: map props, fences (# tiles), trees (T tiles). Lighting is baked
// once per instance (colour arrays shared by instances with the same model and rotation).
const int MAX_STATICS = 320;
const int COLOR_POOL = 48 * 1024;
struct StaticInst {
    int      model;
    Mat34    world;
    const uint32_t* colors;
    float    x, z;            // world position (2D reject, fade test)
    bool     fadeable;        // fades (TR, front faces) while it covers the hero on screen
    float    alpha;           // current fade (1 = opaque)
    float    shadow;          // blob shadow radius (0 = none)
};
static StaticInst g_statics[MAX_STATICS];
static int g_nstatics = 0;
static uint32_t g_color_pool[COLOR_POOL];
static int g_color_used = 0;
struct LitKey { int model; int yaw_q; const uint32_t* colors; };
static LitKey g_lit[MAX_STATICS];
static int g_nlit = 0;
static bool g_scene_built = false;

static const uint32_t* lit_colors(int model, float yaw_rad, const Mat34& world) {
    const int yq = (int)floorf(yaw_rad * (180.0f / 3.14159265f) + 0.5f);      // 1-degree buckets
    for (int i = 0; i < g_nlit; ++i)
        if (g_lit[i].model == model && g_lit[i].yaw_q == yq) return g_lit[i].colors;
    const dmf::Model& m = g_models[model].m;
    if (g_color_used + m.h->num_verts > COLOR_POOL || g_nlit >= MAX_STATICS) return nullptr;
    uint32_t* c = g_color_pool + g_color_used;
    g_color_used += m.h->num_verts;
    prelight(m, world, default_light(), c);
    g_lit[g_nlit++] = LitKey{ model, yq, c };
    return c;
}

// Small deterministic hash for per-tile variation (tree yaw/scale): no RNG state involved.
static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

static void add_static(int model, float x, float z, float yaw_deg, float scale, bool fadeable, float shadow) {
    if (model < 0 || g_nstatics >= MAX_STATICS) return;
    StaticInst& s = g_statics[g_nstatics];
    const float yaw = yaw_deg * (3.14159265f / 180.0f);
    s.model = model;
    s.world = mat_trs_y(v3(x, 0.0f, z), yaw, scale, scale, scale);
    s.colors = lit_colors(model, yaw, s.world);
    if (!s.colors) return;
    s.x = x; s.z = z;
    const dmf::Model& m = g_models[model].m;
    const float ext_x = (m.h->bmax[0] - m.h->bmin[0]) * 0.5f * scale, ext_z = (m.h->bmax[2] - m.h->bmin[2]) * 0.5f * scale;
    (void)ext_x; (void)ext_z;
    s.fadeable = fadeable;
    s.alpha = 1.0f;
    s.shadow = shadow;
    ++g_nstatics;
}

static void build_scene() {
    const FieldState& f = field_state();
    g_nstatics = 0; g_color_used = 0; g_nlit = 0;
    char msg[160];
    // map props
    for (int i = 0; i < f.props.size(); ++i) {
        const Prop& p = f.props[i];
        // <model>_r<yaw>.dmf, if present, is the same model with only the faces the game camera
        // can see at that yaw (tools/dmf/build_village.py, view culling); <model>.dmf is safe at
        // any yaw. Same look, fewer vertices. There are no distance LODs.
        int mi = -1;
        const float yaw_n = fmodf(fmodf(p.rot_deg, 360.0f) + 360.0f, 360.0f);
        if (yaw_n == (float)(int)yaw_n) {
            char vname[24], vpath[48];
            snprintf(vname, sizeof vname, "%s_r%d", p.model, (int)yaw_n);
            snprintf(vpath, sizeof vpath, "/rd/models/%s.dmf", vname);
            if (eng::file_exists(vpath)) mi = model_get(vname);
        }
        if (mi < 0) mi = model_get(p.model);
        if (mi < 0) {
            snprintf(msg, sizeof msg, "field3d: prop model '%s' missing (/rd/models/%s.dmf)", p.model, p.model);
            eng::dbg_print(msg);
            continue;
        }
        const bool house = strncmp(p.model, "house", 5) == 0;
        if (house && !g_models[mi].faded.m) make_faded(g_models[mi]);
        const dmf::Model& m = g_models[mi].m;
        const float r = 0.45f * ((m.h->bmax[0] - m.h->bmin[0]) > (m.h->bmax[2] - m.h->bmin[2]) ? (m.h->bmax[0] - m.h->bmin[0]) : (m.h->bmax[2] - m.h->bmin[2]));
        add_static(mi, p.x, p.y, p.rot_deg, 1.0f, house, house ? 0.0f : (r < 1.4f ? r : 1.4f));
    }
    // fences: a segment between every pair of neighbouring # tiles (joins at corners)
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            if (f.tiles[y * f.w + x] != TILE_WALL) continue;
            if (x + 1 < f.w && f.tiles[y * f.w + x + 1] == TILE_WALL) add_static(g_fence, (float)x + 1.0f, (float)y + 0.5f, 0.0f, 1.0f, false, 0.0f);
            if (y + 1 < f.h && f.tiles[(y + 1) * f.w + x] == TILE_WALL) add_static(g_fence, (float)x + 0.5f, (float)y + 1.0f, 90.0f, 1.0f, false, 0.0f);
        }
    // trees on T tiles: yaw and size vary per tile (deterministic)
    if (g_tree >= 0 && !g_models[g_tree].faded.m) make_faded(g_models[g_tree]);
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            if (f.tiles[y * f.w + x] != TILE_TREE) continue;
            const uint32_t h = hash2(x, y);
            add_static(g_tree, (float)x + 0.5f, (float)y + 0.5f, (float)(h % 8) * 45.0f, 0.85f + 0.1f * (float)((h >> 8) % 4), true, 0.75f);
        }
    snprintf(msg, sizeof msg, "field3d: %d static instances, %d prelit colour sets, %d/%d colours, %d models, %d KiB model arena",
             g_nstatics, g_nlit, g_color_used, COLOR_POOL, g_nmodels, dmf::loaded_bytes() / 1024);
    eng::dbg_print(msg);
    g_scene_built = true;
}

// ---------------------------------------------------------------------------- view state
struct Locomotion { float phase, move_w, run_w, idle_t, speed; };
struct View {
    Vec3  focus;                       // camera follows this point on the ground
    float yaw;                         // hero facing (0 = +Z, towards the camera)
    Locomotion hero;
    float prev_x, prev_z;
    float ent_yaw[MAX_ENTITIES];
    Locomotion ent[MAX_ENTITIES];
    int   ent_px[MAX_ENTITIES], ent_py[MAX_ENTITIES];
};
static View g_v;
static bool g_inited = false;

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

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Advances a locomotion state by distance d (tiles) this tick. Walk and run share one phase
// (both clips start on the left foot), blended by speed; the phase follows distance so the feet
// stay planted, up to the cadence cap.
static void locomotion_tick(Locomotion& L, const Clips& c, float d, float scale) {
    const bool moving = d > 1e-4f;
    L.speed += (d * 60.0f - L.speed) * 0.25f;                       // tiles/s, smoothed
    L.move_w = approach(L.move_w, moving ? 1.0f : 0.0f, moving ? MOVE_IN : MOVE_OUT);
    L.run_w = c.run >= 0 ? clamp01((L.speed - RUN_FROM) / (RUN_FULL - RUN_FROM)) : 0.0f;
    const float stride = (c.walk_stride + (c.run_stride - c.walk_stride) * L.run_w) * scale;
    const float len = c.walk_len + (c.run_len - c.walk_len) * L.run_w;
    float step = d / stride;
    const float cap = MAX_RATE / (60.0f * len);
    L.phase += step < cap ? step : cap;
    L.phase -= floorf(L.phase);
    L.idle_t += 1.0f / 60.0f;
    if (L.idle_t > 3600.0f) L.idle_t -= 3600.0f;
}

static void locomotion_pose(const dmf::Model& m, const Clips& c, const Locomotion& L, dmf::Pose* pose) {
    static dmf::Pose walk, run;
    dmf::pose_sample(m, c.idle, L.idle_t, pose);
    if (L.move_w <= 0.0f || c.walk < 0) return;
    dmf::pose_sample(m, c.walk, L.phase * c.walk_len, &walk);
    if (L.run_w > 0.0f && c.run >= 0) {
        dmf::pose_sample(m, c.run, L.phase * c.run_len, &run);
        dmf::pose_blend(&walk, run, m.h->num_bones, L.run_w);
    }
    dmf::pose_blend(pose, walk, m.h->num_bones, L.move_w);
}

static void field_camera(Camera* cam) {
    const float pitch = CAM_PITCH * 3.14159265f / 180.0f;
    Vec3 look = g_v.focus + v3(0, CAM_LOOK_H, 0);
    camera_look(cam, look + v3(0, CAM_DIST * sinf(pitch), CAM_DIST * cosf(pitch)), look, CAM_FOV_Y);
}

// ---------------------------------------------------------------------------- lifecycle
void field3d_init() {
    if (!g_inited) {
        g_inited = true;
        eng::set_clear_color(CLEAR_RGB);
        g_knight = model_get("knight");
        g_warrior = model_get("skel_warrior");
        g_mage = model_get("skel_mage");
        g_fence = model_get("fence");
        g_tree = model_get("tree");
        g_hero_clips = clips_of(g_knight);
        g_enemy_clips[0] = clips_of(g_warrior);
        g_enemy_clips[1] = clips_of(g_mage);
        if (g_knight >= 0 && g_models[g_knight].m.h->num_verts <= 2048) {
            static dmf::Pose pose;
            static Mat34 skin[dmf::MAX_BONES];
            dmf::pose_sample(g_models[g_knight].m, g_hero_clips.idle, 0.0f, &pose);
            dmf::pose_skin(g_models[g_knight].m, pose, skin);
            for (int b = 0; b < HERO_YAW_BUCKETS; ++b)
                prelight_posed(g_models[g_knight].m, skin, 6.28318531f * (float)b / HERO_YAW_BUCKETS, default_light(), g_hero_lit[b]);
            g_hero_lit_ok = true;
        }
        static uint32_t lit_pool[2][ENEMY_YAW_BUCKETS][2048];
        for (int k = 0; k < 2; ++k) {
            const int mi = k == 0 ? g_warrior : g_mage;
            if (mi < 0 || g_models[mi].m.h->num_verts > 2048) continue;
            static dmf::Pose pose;
            static Mat34 skin[dmf::MAX_BONES];
            dmf::pose_sample(g_models[mi].m, g_enemy_clips[k].idle, 0.0f, &pose);
            dmf::pose_skin(g_models[mi].m, pose, skin);
            for (int b = 0; b < ENEMY_YAW_BUCKETS; ++b) {
                prelight_posed(g_models[mi].m, skin, 6.28318531f * (float)b / ENEMY_YAW_BUCKETS, default_light(), lit_pool[k][b]);
                g_enemy_lit[k][b] = lit_pool[k][b];
            }
        }
        eng::boot_log("field3d: characters loaded and lit; ground texture");
        g_ground_mat = eng::material_create(dmf::shared_texture("/rd/tex/ground.png", false), eng::MAT_BILINEAR);
        g_shadow_mat = eng::material_create(0, eng::MAT_TRANSLUCENT);
    }
    eng::boot_log("field3d: building the static scene (props, fences, trees)");
    build_scene();                      // per map (field_load)
}

void field3d_reset() {
    const FieldState& f = field_state();
    float hx, hz;
    hero_world(f.hero, &hx, &hz);
    memset(&g_v, 0, sizeof g_v);
    g_v.focus = v3(hx, 0, hz);
    g_v.yaw = dir_yaw(f.hero.dir);
    g_v.prev_x = hx; g_v.prev_z = hz;
    for (int i = 0; i < f.ents.size(); ++i) {
        g_v.ent_yaw[i] = dir_yaw(f.ents[i].w.dir);
        field_entity_pixel(i, &g_v.ent_px[i], &g_v.ent_py[i]);
        g_v.ent[i].idle_t = 0.37f * (float)i;                       // desynchronise the idles
    }
    for (int i = 0; i < g_nstatics; ++i) g_statics[i].alpha = 1.0f;
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
    locomotion_tick(g_v.hero, g_hero_clips, d, HERO_SCALE);
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
    for (int i = 0; i < f.ents.size(); ++i) {
        g_v.ent_yaw[i] += angle_diff(g_v.ent_yaw[i], dir_yaw(f.ents[i].w.dir)) * 0.2f;
        int px, py;
        field_entity_pixel(i, &px, &py);
        const float ed = sqrtf((float)((px - g_v.ent_px[i]) * (px - g_v.ent_px[i]) + (py - g_v.ent_py[i]) * (py - g_v.ent_py[i]))) / (float)TILE_PX;
        g_v.ent_px[i] = px; g_v.ent_py[i] = py;
        locomotion_tick(g_v.ent[i], g_enemy_clips[f.ents[i].sheet == SHEET_SLIME ? 0 : 1], ed > 1.0f ? 0.0f : ed, ENEMY_SCALE);
    }
    // Houses and trees that cover the hero on screen fade out: the prop's projected box overlaps
    // the hero's screen rectangle and the prop is nearer the camera than the hero.
    Camera cam;
    field_camera(&cam);
    const Vec3 feet = mat_point(cam.proj_view, v3(hx, 0.0f, hz));
    const Vec3 head = mat_point(cam.proj_view, v3(hx, 1.9f * HERO_SCALE / 0.8f, hz));
    float hx0 = 1e9f, hx1 = -1e9f, hy0 = 1e9f, hy1 = -1e9f;
    bool hero_ok = feet.z > cam.near_w && head.z > cam.near_w;
    if (hero_ok) {
        const float fx = cam.cx + feet.x / feet.z, fy = cam.cy + feet.y / feet.z;
        const float tx = cam.cx + head.x / head.z, ty = cam.cy + head.y / head.z;
        const float halfw = 0.35f * (fy - ty);                 // the Knight is about 0.7 as wide as tall
        hx0 = (fx < tx ? fx : tx) - halfw; hx1 = (fx > tx ? fx : tx) + halfw;
        hy0 = ty; hy1 = fy;
    }
    const float hero_w = feet.z;                               // W of the hero's feet (depth)
    for (int i = 0; i < g_nstatics; ++i) {
        StaticInst& s = g_statics[i];
        if (!s.fadeable) continue;
        bool hides = false;
        if (hero_ok && fabsf(s.x - hx) < 8.0f && s.z > hz - 1.0f && s.z - hz < 12.0f) {
            const dmf::Model& m = g_models[s.model].m;
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, wmin = 1e9f;
            bool behind_cam = false;
            for (int k = 0; k < 8; ++k) {
                const Vec3 p = mat_point(s.world, v3((k & 1) ? m.h->bmax[0] : m.h->bmin[0], (k & 2) ? m.h->bmax[1] : m.h->bmin[1],
                                                     (k & 4) ? m.h->bmax[2] : m.h->bmin[2]));
                const Vec3 c = mat_point(cam.proj_view, p);
                if (c.z < cam.near_w) { behind_cam = true; break; }
                const float sx = cam.cx + c.x / c.z, sy = cam.cy + c.y / c.z;
                if (sx < x0) x0 = sx;
                if (sx > x1) x1 = sx;
                if (sy < y0) y0 = sy;
                if (sy > y1) y1 = sy;
                if (c.z < wmin) wmin = c.z;
            }
            // nearer than the hero (its nearest corner in front of his feet) and overlapping him
            hides = !behind_cam && wmin < hero_w - 0.3f && x0 < hx1 && x1 > hx0 && y0 < hy1 && y1 > hy0 &&
                    s.z > hz + 0.3f;
        }
        s.alpha = approach(s.alpha, hides ? FADE_ALPHA / 255.0f : 1.0f, FADE_RATE);
    }
}

bool field3d_tune(const char* name, float value) {
    if (strcmp(name, "strip_cull") == 0) { set_strip_culling(value != 0.0f); return true; }
    for (const Tune& t : TUNES)
        if (strcmp(t.name, name) == 0) { *t.value = value; return true; }
    return false;
}

Field3dInfo field3d_info() {
    Field3dInfo i = { g_knight >= 0, g_warrior >= 0, g_mage >= 0, g_v.focus.x, g_v.focus.z, g_v.yaw, g_v.hero.move_w };
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

// Distance darkening of the ground (cheap depth cue), as a packed grey.
static uint32_t ground_shade(float dist) {
    float c = 1.0f - (dist - 13.0f) * 0.022f;
    c = c > 1.0f ? 1.0f : (c < 0.55f ? 0.55f : c);
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
        g.argb = ground_shade(sqrtf(dx * dx + cam.eye.y * cam.eye.y + dz * dz));
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
    // ground.png: 4x4 cells of 64x64 texels; inset one texel so bilinear never reads a neighbour
    const float CELL = 0.25f, INSET = 1.0f / 256.0f;
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
static void draw_statics(const Camera& cam) {
    const float fx = g_v.focus.x, fz = g_v.focus.z;
    for (int i = 0; i < g_nstatics; ++i) {
        const StaticInst& s = g_statics[i];
        if (fabsf(s.x - fx) > DRAW_RANGE_X || fabsf(s.z - fz) > DRAW_RANGE_Z) continue;
        const LoadedModel& lm = g_models[s.model];
        const uint32_t* colors = s.colors;
        if (s.alpha < 0.999f) {
            // faded (hiding the hero): same prelit colours with the alpha replaced, TR list
            static uint32_t faded[4096];
            const int nv = lm.m.h->num_verts < 4096 ? lm.m.h->num_verts : 4096;
            const uint32_t a = (uint32_t)(s.alpha * 255.0f) << 24;
            for (int k = 0; k < nv; ++k) faded[k] = (colors[k] & 0x00FFFFFFu) | a;
            if (!lm.faded.m) continue;
            draw_static(cam, lm.faded, s.world, faded);
        } else if (!draw_static(cam, lm.gfx, s.world, colors)) {
            continue;                                   // culled: no shadow either
        }
        if (s.shadow > 0.0f) draw_blob_shadow(cam, g_shadow_mat, v3(s.x, 0, s.z), s.shadow, (int)(SHADOW_ALPHA * 0.8f));
    }
}

void field3d_draw(int hero_hp, int hero_hp_max) {
    const FieldState& f = field_state();
    stats_reset();
    Camera cam;
    field_camera(&cam);
    const Light light = default_light();

    // Characters first, scenery last: if the frame's vertex arena ever fills up, it is props that
    // drop out, never the hero (the z-buffer makes the order irrelevant to the picture).
    draw_ground(cam);

    // enemies: slime -> skeleton warrior, everything else (the guard) -> skeleton mage
    static dmf::Pose pose;
    static Mat34 skin[dmf::MAX_BONES];
    for (int i = 0; i < f.ents.size(); ++i) {
        const Entity& e = f.ents[i];
        if (!e.alive) continue;
        const int kind = e.sheet == SHEET_SLIME ? 0 : 1;
        const int mi = kind == 0 ? g_warrior : g_mage;
        float x, z;
        entity_world(i, &x, &z);
        if (mi >= 0) {
            locomotion_pose(g_models[mi].m, g_enemy_clips[kind], g_v.ent[i], &pose);
            dmf::pose_skin(g_models[mi].m, pose, skin);
            const int bucket = yaw_bucket(g_v.ent_yaw[i], ENEMY_YAW_BUCKETS);
            draw_model(cam, light, g_models[mi].gfx, mat_trs_y(v3(x, 0, z), g_v.ent_yaw[i], ENEMY_SCALE, ENEMY_SCALE, ENEMY_SCALE), skin,
                       0xFFFFFFFFu, g_enemy_lit[kind][bucket]);
        }
        draw_blob_shadow(cam, g_shadow_mat, v3(x, 0, z), 0.55f * ENEMY_SCALE / 0.8f, (int)SHADOW_ALPHA);
    }

    // hero
    float hx, hz;
    hero_world(f.hero, &hx, &hz);
    if (g_knight >= 0) {
        const dmf::Model& m = g_models[g_knight].m;
        locomotion_pose(m, g_hero_clips, g_v.hero, &pose);
        dmf::pose_skin(m, pose, skin);
        const uint32_t* lit = (g_hero_lit_ok && HERO_DYN_LIGHT < 0.5f) ? g_hero_lit[yaw_bucket(g_v.yaw, HERO_YAW_BUCKETS)] : nullptr;
        draw_model(cam, light, g_models[g_knight].gfx, mat_trs_y(v3(hx, 0, hz), g_v.yaw, HERO_SCALE, HERO_SCALE, HERO_SCALE), skin,
                   0xFFFFFFFFu, lit);
    }
    draw_blob_shadow(cam, g_shadow_mat, v3(hx, 0, hz), 0.62f * HERO_SCALE / 0.8f, (int)SHADOW_ALPHA);

    draw_statics(cam);

    // HUD: hero HP (top right)
    char hud[24];
    snprintf(hud, sizeof hud, "HP %d/%d", hero_hp, hero_hp_max);
    eng::gfx_rect_tr(500, 12, 128, 28, 0xA0000000, 7.0f);
    font_draw(510, 18, hud, 2, hero_hp * 4 > hero_hp_max ? 0xFFF4F4F4 : 0xFFEF7D57, true, 7.5f);
    if (g_knight < 0) font_draw(20, 440, "HERO MODEL MISSING  SEE SERIAL LOG", 2, 0xFFFFCD75, true, 7.5f);
}

} // namespace bt
