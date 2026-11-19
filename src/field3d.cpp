// field3d.cpp - see field3d.h
#include "field3d.h"
#include "anim.h"
#include "assets.h"
#include "battle3d.h"
#include "dialog_ui.h"
#include "lights.h"
#include "dmf.h"
#include "field.h"
#include "font.h"
#include "icons.h"
#include "models.h"
#include "render3d.h"
#include "text.h"
#include "uitext.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace bt {

using namespace r3d;

// ---- tuning (expect these to move in the next style passes) ------------------------------
// Plain variables so the host harness can try values without a rebuild (field3d_tune,
// trace_main --tune name=value). The Dreamcast build uses these defaults.
struct Tune { const char* name; float* value; };
// The field camera is VS1's (docs/SLICE_VS1_ART.md): 9 up and 9 back from the look-at point (pitch 45,
// 12.73 units), a narrow 32-degree lens (a diorama: little foreground blow-up, at most ~3 houses in view),
// aimed 1.5 tiles north of the hero and leading him along his motion, clamped so the view stays on the map.
static float CAM_FOV_Y     = 32.0f;   // degrees
static float CAM_PITCH     = 45.0f;   // degrees below horizontal
static float CAM_DIST      = 12.73f;  // units from the look-at point (9 up, 9 back)
static float CAM_LOOK_H    = 0.55f;   // look-at height above the ground
static float CAM_AHEAD     = 1.5f;    // look-at point this far north of the hero (he sits below the centre)
static float CAM_FOLLOW    = 0.14f;   // fraction of the remaining offset closed per tick
static float CAM_LEAD      = 1.6f;    // tiles the camera leads the hero along his motion (at full speed)
static float CAM_LEAD_RATE = 0.035f;  // per tick, how fast the lead builds up / decays
static float CAM_CLAMP_SIDE = 4.5f;   // the look-at point stays this far inside the map's sides...
static float CAM_CLAMP_TOP  = 7.0f;   // ...its north edge (the top of the screen reaches ~8 tiles north of it)...
static float CAM_CLAMP_BOTTOM = 3.0f; // ...and its south edge
static float HERO_SCALE    = 0.66f;   // x KayKit units: the Knight is ~1.55 tiles tall (VS1's character height:
static float ENEMY_SCALE   = 0.66f;   //   the kit's doors and windows read right at this size; the battle keeps 0.8)
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
static float NEAR_FADE_DIST  = 5.0f;  // VS1: a hiding prop closer than this to the eye fades further...
static float NEAR_FADE_MIN   = 31.0f; // ...down to this alpha (0..255, VS1's 0.12) at NEAR_FADE_CLOSE units: a ghost, not a wall
static float NEAR_FADE_CLOSE = 2.0f;  //    (the VS1 camera is 9 up: a house south of the hero fills the lower screen)
static float ALPHA_SKIP    = 0.08f;   // below this a faded prop is not drawn at all (also: the eye inside its box)
static float HERO_DYN_LIGHT = 0.0f;   // 1 = light the hero per vertex every frame (~200k SH4
                                      // instructions); 0 = lighting baked per yaw (32 buckets, idle pose)
// slice 3D-5: the dialog two-shot (docs/NEXT_SLICE_DIALOG.md 5.3) and the billboards (5.4)
static float DLG_PITCH     = 32.0f;   // degrees below horizontal (VS1 art: 38 looked at Wren's hat brim; 32 shows more of the pair)
static float DLG_DIST      = 7.5f;    // units from the look-at point (the VS1-scale characters are 0.66, not 0.8)
static float DLG_FOV       = 40.0f;   // degrees
static float DLG_YAW       = 45.0f;   // most degrees the shot turns from the field camera's heading
static float DLG_LOOK_H    = 0.85f;   // look-at height: the pair's heads land at y ~150-200, above the box
static float DLG_MARGIN    = 0.3f;    // the eye must stay this far out of every prop's box
static float NPC_TURN      = 0.20f;   // NPC turn per tick (fraction of the remaining angle; the enemies' rate)
static float MARK_LIFT     = 0.45f;   // markers and emotes: units above the head
static float MARK_SIZE     = 0.62f;   // billboard size (units)
static const Tune TUNES[] = {
    { "cam_fov", &CAM_FOV_Y }, { "cam_pitch", &CAM_PITCH }, { "cam_dist", &CAM_DIST },
    { "cam_look_h", &CAM_LOOK_H }, { "cam_follow", &CAM_FOLLOW }, { "cam_ahead", &CAM_AHEAD }, { "cam_lead", &CAM_LEAD },
    { "cam_lead_rate", &CAM_LEAD_RATE }, { "cam_clamp_side", &CAM_CLAMP_SIDE }, { "cam_clamp_top", &CAM_CLAMP_TOP },
    { "cam_clamp_bottom", &CAM_CLAMP_BOTTOM }, { "hero_scale", &HERO_SCALE },
    { "enemy_scale", &ENEMY_SCALE }, { "turn_rate", &TURN_RATE }, { "turn_max", &TURN_MAX },
    { "move_in", &MOVE_IN }, { "move_out", &MOVE_OUT }, { "run_from", &RUN_FROM }, { "run_full", &RUN_FULL },
    { "max_rate", &MAX_RATE }, { "shadow_alpha", &SHADOW_ALPHA }, { "fade_alpha", &FADE_ALPHA },
    { "fade_rate", &FADE_RATE }, { "hero_dyn_light", &HERO_DYN_LIGHT }, { "near_fade_dist", &NEAR_FADE_DIST },
    { "near_fade_min", &NEAR_FADE_MIN }, { "near_fade_close", &NEAR_FADE_CLOSE },
    { "dlg_pitch", &DLG_PITCH }, { "dlg_dist", &DLG_DIST }, { "dlg_fov", &DLG_FOV }, { "dlg_yaw", &DLG_YAW },
    { "dlg_look_h", &DLG_LOOK_H }, { "dlg_margin", &DLG_MARGIN }, { "npc_turn", &NPC_TURN },
    { "mark_lift", &MARK_LIFT }, { "mark_size", &MARK_SIZE },
    // cel-shading feasibility prototype (render3d.h CelParams; docs/CEL_SHADING_STUDY.md)
    { "cel_outline_px", &r3d::cel().outline_px }, { "cel_chars", &r3d::cel().chars }, { "cel_props", &r3d::cel().props },
    { "cel_toon", &r3d::cel().toon }, { "cel_band_lo", &r3d::cel().band_lo }, { "cel_band_hi", &r3d::cel().band_hi },
    { "cel_rgb", &r3d::cel().outline_rgb }, { "cel_lod", &r3d::cel().lod }, { "cel_zpush", &r3d::cel().zpush },
    { "cel_weapons", &r3d::cel().weapons },
};
const int   BORDER_TILES  = 10;      // ground drawn around the map so the view never shows a void
const float GROUND_FAR    = 40.0f;   // ground is not drawn beyond this distance from the eye
const float DRAW_RANGE_X  = 15.0f;   // props farther than this from the camera focus are skipped
const float DRAW_RANGE_Z  = 18.0f;   //   before the per-instance frustum test (cheap 2D reject)
const uint32_t CLEAR_RGB  = 0x24403A;   // = the outer ground (VS1): beyond the drawn border the colour goes on

// ---------------------------------------------------------------------------- models
// Every model the field shows comes from the shared registry (models.h): loaded once per session.
static int model_get(const char* name) { return models_get(name); }
static void make_faded(LoadedModel& lm) { models_make_faded(lm); }

static int g_knight = -1;
const int HERO_YAW_BUCKETS = 32;
const int MAX_CHAR_VERTS = 2304;     // a character's vertices (the Knight with VS1's head: 2,280): lighting buffers
static uint32_t g_hero_lit[HERO_YAW_BUCKETS][MAX_CHAR_VERTS];
static bool g_hero_lit_ok = false;

static int yaw_bucket(float yaw, int buckets) {
    int b = (int)shz_floorf(yaw * ((float)buckets / 6.28318531f) + 0.5f) % buckets;
    return b < 0 ? b + buckets : b;
}
static int g_fence = -1, g_tree = -1;
struct Clips { int idle, walk, run; float walk_len, run_len, walk_stride, run_stride; };
static Clips g_hero_clips;
static eng::Material g_shadow_mat = 0, g_bill_mat = 0;

static Clips clips_of(int mi) {
    Clips c = { -1, -1, -1, 1.0f, 1.0f, 1.0f, 1.0f };
    if (mi < 0) return c;
    const dmf::Model& m = models_at(mi).m;
    c.idle = dmf::anim_find(m, "idle");
    c.walk = dmf::anim_find(m, "walk");
    c.run = dmf::anim_find(m, "run");
    if (c.walk >= 0) { c.walk_len = dmf::anim_duration(m, c.walk); c.walk_stride = m.anims[c.walk].ground_speed * c.walk_len; }
    if (c.run >= 0) { c.run_len = dmf::anim_duration(m, c.run); c.run_stride = m.anims[c.run].ground_speed * c.run_len; }
    if (!(c.walk_stride > 0.0f)) c.walk_stride = 1.0f;
    if (!(c.run_stride > 0.0f)) c.run_stride = c.walk_stride;
    return c;
}

// Entity looks (field.h EntityLook). The skeletons carry their field clips (idle, walk) in the .dmf;
// Wren and Brom use the shared library (slice 3D-5, decision D9): idle_a, walk, interact, use_item.
// Every look is also bound to the library, so the script's `anim` plays any clip on anyone.
// Slice 3D-6: scale (x ENEMY_SCALE: the Bone Warden is 1.5x, GDD 2.2) and static looks (the waystone: a prop-like
// model without a skeleton, drawn with baked colours; decision BA1).
struct LookDef { const char* model; bool library; float scale; bool is_static; };
// The patrols are the VS1 Skeleton_Warrior (docs/SLICE_VS1_ART.md: 2,885 triangles, skull and spiked helmet intact;
// the battle keeps its 788-triangle skel_warrior, three of which share a frame).
static const LookDef LOOKS[ELOOK_COUNT] = {
    { "skel_warrior", false, 1.0f, false }, { "skel_mage", false, 1.0f, false }, { "wren", true, 1.0f, false },
    { "brom", true, 1.0f, false }, { "corwin", true, 1.0f, false }, { "skel_mage", false, 1.5f, false },
    { "waystone", false, 1.5f, true } };   // the stone: 1.5 x 0.66 = about world scale
// Enemies and NPCs only turn in yaw: their lighting is baked per yaw bucket (idle-pose normals). Looks that share a
// model (the guard and the Warden) share the baked colours.
const int ENEMY_YAW_BUCKETS = 16;
const int LIT_POOL_VERTS = 9728;     // sum of the distinct models' vertex counts (skeleton 2,007, skel_mage 1,671, Wren 2,047,
                                     // Brom 1,903, Corwin ~2,000; slice 3D-6 added Corwin)
struct LookRt {
    int model;
    Clips clips;                     // embedded (skeletons) or library clip ids and lengths (NPCs)
    anim::Binding bind;
    bool bound;
    float top;                       // head-top height (units, at the look's scale): markers and emotes
    float scale;                     // ENEMY_SCALE x LOOKS[].scale
    int  shares;                     // the look whose baked colours this one uses (-1: its own)
    int  idle_embedded, idle_library;   // the bake pose
    uint32_t* lit[ENEMY_YAW_BUCKETS];
};
static LookRt g_look[ELOOK_COUNT];
static uint32_t g_lit_pool[ENEMY_YAW_BUCKETS * LIT_POOL_VERTS];
static anim::Binding g_knight_bind;
static bool g_knight_bound = false;
static const anim::Library* g_lib = nullptr;
static int g_clip_interact = -1;
static int g_clip_awaken = -1;         // slice 3D-6: an `asleep` entity holds its first frame (lying) until woken

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
static float g_static_box[MAX_STATICS][6];   // world AABB: min xyz, max xyz
static int g_nstatics = 0;
static uint32_t g_color_pool[COLOR_POOL];
static int g_color_used = 0;
struct LitKey { int model; int yaw_q; uint32_t* colors; Mat34 world; };
static LitKey g_lit[MAX_STATICS];
static int g_nlit = 0;
static bool g_scene_built = false;

// ---- slice 3D-6: the light preset in effect (lights.h)
static LightPreset g_lp;               // what is baked (or, during a lerp, the current blend)
static int  g_lp_id = 0;
static Light g_light;                  // g_lp as the renderer's light
struct LightLerp { bool on; int from, to, t, len; };
static LightLerp g_lerp = { false, 0, 0, 0, 0 };
static bool g_chars_dynamic = false;   // characters lit per frame (a lerp runs, or a re-bake is pending)
static int  g_bake_next = -1;          // re-bake queue after a lerp: next item (-1: none pending)

static Light light_of(const LightPreset& p) {
    Light l;
    l.dir = v3(p.dir[0], p.dir[1], p.dir[2]);
    l.ambient = p.ambient; l.diffuse = p.diffuse;
    return l;
}

// One colour set of a static model under the current preset: runtime-lit models take the light and its colour,
// baked ones (PRELIT) the tint.
static void light_static(const LitKey& k) {
    const dmf::Model& m = models_at(k.model).m;
    prelight(m, k.world, g_light, k.colors);
    const uint32_t mul = (m.h->flags & dmf::DMF_PRELIT) ? g_lp.tint : g_lp.colour;
    if ((mul & 0xFFFFFFu) != 0xFFFFFFu)
        for (int i = 0; i < m.h->num_verts; ++i) k.colors[i] = rgb_mul(k.colors[i], mul);
}

static const uint32_t* lit_colors(int model, float yaw_rad, const Mat34& world) {
    // 1-degree buckets; baked models (PRELIT, the VS1 village art) have one colour set whatever the yaw
    const int yq = (models_at(model).m.h->flags & dmf::DMF_PRELIT) ? 0 : (int)shz_floorf(yaw_rad * (180.0f / 3.14159265f) + 0.5f);
    for (int i = 0; i < g_nlit; ++i)
        if (g_lit[i].model == model && g_lit[i].yaw_q == yq) return g_lit[i].colors;
    const dmf::Model& m = models_at(model).m;
    if (g_color_used + m.h->num_verts > COLOR_POOL || g_nlit >= MAX_STATICS) return nullptr;
    uint32_t* c = g_color_pool + g_color_used;
    g_color_used += m.h->num_verts;
    g_lit[g_nlit] = LitKey{ model, yq, c, world };
    light_static(g_lit[g_nlit]);
    ++g_nlit;
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
    const dmf::Model& m = models_at(model).m;
    const float ext_x = (m.h->bmax[0] - m.h->bmin[0]) * 0.5f * scale, ext_z = (m.h->bmax[2] - m.h->bmin[2]) * 0.5f * scale;
    (void)ext_x; (void)ext_z;
    s.fadeable = fadeable;
    s.alpha = 1.0f;
    s.shadow = shadow;
    float* box = g_static_box[g_nstatics];               // world AABB (slice 3D-5: the dialog camera's eye test)
    box[0] = box[1] = box[2] = 1e9f; box[3] = box[4] = box[5] = -1e9f;
    for (int k = 0; k < 8; ++k) {
        const Vec3 c = mat_point(s.world, v3((k & 1) ? m.h->bmax[0] : m.h->bmin[0], (k & 2) ? m.h->bmax[1] : m.h->bmin[1],
                                             (k & 4) ? m.h->bmax[2] : m.h->bmin[2]));
        const float cc[3] = { c.x, c.y, c.z };
        for (int a = 0; a < 3; ++a) { box[a] = cc[a] < box[a] ? cc[a] : box[a]; box[3 + a] = cc[a] > box[3 + a] ? cc[a] : box[3 + a]; }
    }
    ++g_nstatics;
}

// ---------------------------------------------------------------------------- ground texture
// The VS1 village ground (assets/vs1_art, docs/SLICE_VS1_ART.md), ported from the VS1 lineage's field3d.cpp:
// one texture over the whole map, GROUND_TPT texels per tile, sampled bilinear, so grass and paths blend
// softly instead of showing tile cells. Grass gets low-frequency value noise (~3 tiles) and texel jitter,
// flower tiles get white/yellow specks, a path's edge texels mix with the grass beside them. One texture,
// 256 x 128 RGB565 (64 KiB VRAM), regenerated when a different map loads (texture_update: same size);
// texels outside the map (the power-of-two padding) hold the outer ground colour (the border tiles sample it).
const int GROUND_TW = 256, GROUND_TH = 128;
const uint32_t OUTER_GROUND = 0x24403A;     // beyond the map edge (VS1's; the camera clamp mostly hides it)
static uint16_t g_ground_texels[GROUND_TW * GROUND_TH];   // KOS keeps the pointer of a memory texture: static
static eng::Sprite g_ground_tex = 0;       // the generated (VS1) ground texture
static eng::Material g_gen_mat = 0;
// The ground of the current map: the painted one (romdisk/maps/<name>.gnd, tools/ground/paint_ground.py, a VQ
// texture with a margin of tiles around the map) if the map has one, else the generated one above.
struct GroundView { eng::Material mat; int tw, th, tpt, margin; uint32_t outer_rgb; };
static GroundView g_gv = { 0, GROUND_TW, GROUND_TH, 4, 0, OUTER_GROUND };
static int g_ground_tpt = 4;                // texels per tile of the generated texture
static eng::Sprite g_gnd_tex = 0;
static eng::Material g_gnd_mat = 0;
static char g_gnd_map[32] = "";             // the map the painted texture belongs to
static int  g_gnd_w = 0, g_gnd_h = 0;        // its size (a second painted map must match: the slot is reused)
struct GndHeader { char magic[4]; uint16_t w, h, tpt, margin, map_w, map_h, outer565, flags; uint32_t bytes; uint8_t pad[8]; };
static_assert(sizeof(GndHeader) == 32, "GND1 header");
alignas(32) static uint8_t g_gnd_file[32 + eng::VQ_CODEBOOK_BYTES + 512 * 512 / 4 + 32];   // + room: file_read wants cap > size

// The painted ground of this map, if there is one. Slice 3D-6: a second painted map (the barrow) reuses the first
// one's VQ texture slot (texture_update_vq: same 512x512 size; the map change happens under a full fade, so the
// slot has not been drawn for 2 frames - decision D5).
static bool load_painted_ground() {
    const FieldState& f = field_state();
    if (g_gnd_tex && strcmp(g_gnd_map, f.name) == 0) return true;
    char path[64];
    snprintf(path, sizeof path, "/rd/maps/%s.gnd", f.name);
    if (!eng::file_exists(path)) return false;
    const int n = eng::file_read(path, (char*)g_gnd_file, sizeof g_gnd_file);
    const GndHeader* h = (const GndHeader*)g_gnd_file;
    char msg[200];
    if (n < 32 || memcmp(h->magic, "GND1", 4) || h->flags != 1 || (int)h->bytes != n - 32 || h->map_w != f.w || h->map_h != f.h ||
        h->tpt < 1 || (f.w + 2 * h->margin) * h->tpt > h->w || (f.h + 2 * h->margin) * h->tpt > h->h) {
        snprintf(msg, sizeof msg, "field3d: %s is not a GND1 VQ ground for this %dx%d map: the generated ground instead", path, f.w, f.h);
        eng::dbg_print(msg);
        return false;
    }
    if (g_gnd_tex) {
        if (h->w != g_gnd_w || h->h != g_gnd_h || !eng::texture_update_vq(g_gnd_tex, g_gnd_file + 32, (int)h->bytes)) {
            snprintf(msg, sizeof msg, "field3d: %s does not fit the painted ground's texture slot: the generated ground instead", path);
            eng::dbg_print(msg);
            g_gnd_map[0] = '\0';
            return false;
        }
    } else {
        g_gnd_tex = eng::texture_create_vq(g_gnd_file + 32, (int)h->bytes, h->w, h->h);
        if (!g_gnd_tex) return false;
        g_gnd_w = h->w; g_gnd_h = h->h;
        g_gnd_mat = eng::material_create(g_gnd_tex, eng::MAT_BILINEAR);
    }
    g_gv.tw = h->w; g_gv.th = h->h; g_gv.tpt = h->tpt; g_gv.margin = h->margin;
    const uint32_t o = h->outer565;
    g_gv.outer_rgb = ((o >> 11) * 255 / 31) << 16 | (((o >> 5) & 63) * 255 / 63) << 8 | ((o & 31) * 255 / 31);
    snprintf(g_gnd_map, sizeof g_gnd_map, "%s", f.name);
    snprintf(msg, sizeof msg, "field3d: painted ground %s: %dx%d VQ, %d texels per tile, %d-tile margin", path, h->w, h->h, h->tpt, h->margin);
    eng::dbg_print(msg);
    return true;
}
static char g_ground_map[32] = "";          // the map the texture was made for

static uint16_t rgb565_pct(uint32_t rgb, int pct) {
    int r = (int)((rgb >> 16) & 255) * pct / 100, g = (int)((rgb >> 8) & 255) * pct / 100, b = (int)(rgb & 255) * pct / 100;
    r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint32_t ground_hash(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float ground_noise(float x, float y) {            // value noise in [0, 1] on an integer lattice
    const int xi = (int)shz_floorf(x), yi = (int)shz_floorf(y);
    const float fx = x - (float)xi, fy = y - (float)yi;
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float h00 = (float)(ground_hash(xi, yi) & 1023u) / 1023.0f, h10 = (float)(ground_hash(xi + 1, yi) & 1023u) / 1023.0f;
    const float h01 = (float)(ground_hash(xi, yi + 1) & 1023u) / 1023.0f, h11 = (float)(ground_hash(xi + 1, yi + 1) & 1023u) / 1023.0f;
    const float a = h00 + (h10 - h00) * sx, b = h01 + (h11 - h01) * sx;
    return a + (b - a) * sy;
}

static void build_ground_texture() {
    const FieldState& f = field_state();
    if (load_painted_ground()) {
        g_gv.mat = g_gnd_mat;
        return;
    }
    g_gv = GroundView{ g_gen_mat, GROUND_TW, GROUND_TH, g_ground_tpt, 0, OUTER_GROUND };
    if (g_ground_tex && strcmp(g_ground_map, f.name) == 0) return;     // same map (a retry, a reload): keep it
    int tpt = 8;
    while (tpt > 1 && (f.w * tpt > GROUND_TW || f.h * tpt > GROUND_TH)) tpt >>= 1;
    g_ground_tpt = tpt;
    const int mw = f.w * tpt, mh = f.h * tpt;
    auto tile_of = [&](int tx, int ty) -> uint8_t {
        tx = tx < 0 ? 0 : (tx >= f.w ? f.w - 1 : tx);
        ty = ty < 0 ? 0 : (ty >= f.h ? f.h - 1 : ty);
        return f.tiles[ty * f.w + tx];
    };
    auto is_path = [](uint8_t t) { return t == TILE_PATH; };
    const uint16_t outer = rgb565_pct(OUTER_GROUND, 100);
    for (int y = 0; y < GROUND_TH; ++y)
        for (int x = 0; x < GROUND_TW; ++x) {
            uint16_t& out = g_ground_texels[y * GROUND_TW + x];
            if (x >= mw || y >= mh) { out = outer; continue; }
            const int tx = x / tpt, ty = y / tpt, sx = x % tpt, sy = y % tpt;
            const uint8_t t = tile_of(tx, ty);
            const uint32_t hsh = ground_hash(x, y);
            const float wx = ((float)x + 0.5f) / (float)tpt, wy = ((float)y + 0.5f) / (float)tpt;   // tiles
            const int pct = 86 + (int)(ground_noise(wx / 3.0f, wy / 3.0f) * 22.0f) + (int)(hsh % 7u) - 3;
            uint32_t c;
            switch (t) {
            case TILE_FLOWERS: c = (hsh >> 8) % 11u == 0 ? ((hsh >> 12) & 1 ? 0xF4F4F4u : 0xFFCD75u) : 0x3E9E55u; break;
            case TILE_PATH:    c = 0xC9A46Au; break;
            case TILE_WALL:    c = 0x94B0C2u; break;
            case TILE_TREE:    c = 0x1E6B48u; break;
            case TILE_WATER:
            case TILE_WATER2:  c = (hsh >> 8) % 13u == 0 ? 0x73EFF7u : 0x3B5DC9u; break;
            default:           c = 0x3E9E55u; break;
            }
            // soften path borders: an edge texel next to a tile of the other kind takes a mix
            const bool edge_l = sx == 0, edge_r = sx == tpt - 1, edge_t = sy == 0, edge_b = sy == tpt - 1;
            const bool mixed = (edge_l && is_path(tile_of(tx - 1, ty)) != is_path(t)) || (edge_r && is_path(tile_of(tx + 1, ty)) != is_path(t)) ||
                               (edge_t && is_path(tile_of(tx, ty - 1)) != is_path(t)) || (edge_b && is_path(tile_of(tx, ty + 1)) != is_path(t));
            if (mixed && (hsh >> 4) % 3u != 0) {
                const uint32_t o = is_path(t) ? 0x3E9E55u : 0xC9A46Au;
                c = (((c >> 16 & 255) + (o >> 16 & 255)) / 2) << 16 | (((c >> 8 & 255) + (o >> 8 & 255)) / 2) << 8 | ((c & 255) + (o & 255)) / 2;
            }
            out = rgb565_pct(c, pct);
        }
    if (!g_ground_tex) {
        g_ground_tex = eng::texture_create(g_ground_texels, GROUND_TW, GROUND_TH, 1);
        g_gen_mat = g_ground_tex ? eng::material_create(g_ground_tex, eng::MAT_BILINEAR) : 0;
    } else {
        eng::texture_update(g_ground_tex, g_ground_texels);      // map change: not drawn since the fade-out
    }
    g_gv = GroundView{ g_gen_mat, GROUND_TW, GROUND_TH, g_ground_tpt, 0, OUTER_GROUND };
    snprintf(g_ground_map, sizeof g_ground_map, "%s", f.name);
}

static void build_scene() {
    build_ground_texture();
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
        const float yaw_n = shz_fmodf(shz_fmodf(p.rot_deg, 360.0f) + 360.0f, 360.0f);
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
        // See-through when covering the hero: what VS1's catalog fades (houses, boulders, the wagon, the gate arch);
        // crates, fences and door steps are too low to hide him.
        const bool fades = strncmp(p.model, "house", 5) == 0 || strncmp(p.model, "boulder", 7) == 0 ||
                           strcmp(p.model, "wagon") == 0 || strcmp(p.model, "arch") == 0;
        if (fades && !models_at(mi).faded.m) make_faded(models_at(mi));
        const dmf::Model& m = models_at(mi).m;
        // blob shadows only under props lit at runtime (the VS1 art has its lighting baked, without shadows)
        const bool baked = (m.h->flags & dmf::DMF_PRELIT) != 0;
        const float r = 0.45f * ((m.h->bmax[0] - m.h->bmin[0]) > (m.h->bmax[2] - m.h->bmin[2]) ? (m.h->bmax[0] - m.h->bmin[0]) : (m.h->bmax[2] - m.h->bmin[2]));
        add_static(mi, p.x, p.y, p.rot_deg, 1.0f, fades, (fades || baked) ? 0.0f : (r < 1.4f ? r : 1.4f));
    }
    // fences: a segment between every pair of neighbouring # tiles (joins at corners); slice 3D-6: `fences <model>`
    const int fence_m = f.fence_model[0] ? model_get(f.fence_model) : g_fence;
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            if (f.tiles[y * f.w + x] != TILE_WALL) continue;
            if (x + 1 < f.w && f.tiles[y * f.w + x + 1] == TILE_WALL) add_static(fence_m, (float)x + 1.0f, (float)y + 0.5f, 0.0f, 1.0f, false, 0.0f);
            if (y + 1 < f.h && f.tiles[(y + 1) * f.w + x] == TILE_WALL) add_static(fence_m, (float)x + 0.5f, (float)y + 1.0f, 90.0f, 1.0f, false, 0.0f);
        }
    // trees on T tiles: yaw and size vary per tile (deterministic); slice 3D-6: `trees <model>` (the barrow's dead trees)
    const int tree_m = f.tree_model[0] ? model_get(f.tree_model) : g_tree;
    if (tree_m >= 0 && !models_at(tree_m).faded.m) make_faded(models_at(tree_m));
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            if (f.tiles[y * f.w + x] != TILE_TREE) continue;
            const uint32_t h = hash2(x, y);
            add_static(tree_m, (float)x + 0.5f, (float)y + 0.5f, (float)(h % 8) * 45.0f, 0.85f + 0.1f * (float)((h >> 8) % 4), true, 0.75f);
        }
    snprintf(msg, sizeof msg, "field3d: %d static instances, %d prelit colour sets, %d/%d colours, %d models, %d KiB model arena",
             g_nstatics, g_nlit, g_color_used, COLOR_POOL, models_count(), dmf::loaded_bytes() / 1024);
    eng::dbg_print(msg);
    g_scene_built = true;
}

// ---------------------------------------------------------------------------- view state
struct Locomotion { float phase, move_w, run_w, idle_t, speed; };
struct Overlay { anim::Animator an; float w; bool on; };      // a one-shot library clip over the base pose
const int MAX_ACTORS_3D = 1 + MAX_ENTITIES;                   // 0 = Rowan, 1 + i = entity i
struct View {
    Vec3  focus;                       // camera follows this point on the ground (clamped to the map)
    Vec3  lead;                        // the camera's current lead along the hero's motion (tiles)
    float yaw;                         // hero facing (0 = +Z, towards the camera)
    Locomotion hero;
    float prev_x, prev_z;
    float ent_yaw[MAX_ENTITIES];
    Locomotion ent[MAX_ENTITIES];
    int   ent_px[MAX_ENTITIES], ent_py[MAX_ENTITIES];
    // slice 3D-5
    Overlay ov[MAX_ACTORS_3D];
    int   emote_kind[MAX_ACTORS_3D], emote_t[MAX_ACTORS_3D];
    int   marker[MAX_ENTITIES];
    bool  prompt;
    bool  dlg_ok;                      // the two-shot is usable (else the field camera stays)
    Vec3  dlg_eye, dlg_look;
    Vec3  cam_eye, cam_look;           // the camera of the last tick (info)
    int   clock;                       // ticks (bobbing)
    int   n_markers, n_emotes;         // drawn last frame
    // slice 3D-6
    bool  woken[MAX_ENTITIES];         // an `asleep` entity has been animated (the Warden rising): no more lying pose
};
static View g_v;

// Slice 3D-6: the scripted camera (`cam <shot> <ticks>`: eased with smoothstep from where the camera was; `cam field`
// eases back to the follow camera) and the override (the title's orbit, the credits' flyover: set every tick).
struct ScriptCam { bool on, to_field; Vec3 from_eye, from_look, to_eye, to_look; float from_fov, to_fov; int t, len; };
static ScriptCam g_sc = { false, false, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, 32.0f, 32.0f, 0, 0 };
struct CamOverride { bool on; Vec3 eye, look; float fov; };
static CamOverride g_co = { false, { 0, 0, 0 }, { 0, 0, 0 }, 32.0f };
static bool g_show_hero = true, g_show_ents = true;
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

// Yaw that faces from (x0, z0) toward (x1, z1) (0 = +Z; exact atan2: once per tick, not a hot path).
static float yaw_toward(float x0, float z0, float x1, float z1) { return atan2f(x1 - x0, z1 - z0); }

static float approach(float v, float target, float step) {
    return v < target ? (v + step > target ? target : v + step) : (v - step < target ? target : v - step);
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static float wrap_pi(float a) {
    if (a > 3.14159265f) a -= 6.28318531f;
    if (a < -3.14159265f) a += 6.28318531f;
    return a;
}

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
    float step = shz_divf_fsrra(d, stride);                         // stride, len > 0
    const float cap = shz_divf_fsrra(MAX_RATE, 60.0f * len);
    L.phase += step < cap ? step : cap;
    L.phase -= shz_floorf(L.phase);
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

// The same from the shared library (Wren, Brom): idle_a and walk, blended by the walk weight.
static void library_pose(const LookRt& lk, const dmf::Model& m, const Locomotion& L, dmf::Pose* pose) {
    static dmf::Pose walk;
    anim::sample(*g_lib, lk.bind, m, lk.clips.idle, L.idle_t, pose);
    if (L.move_w <= 0.0f || lk.clips.walk < 0) return;
    anim::sample(*g_lib, lk.bind, m, lk.clips.walk, L.phase * lk.clips.walk_len, &walk);
    dmf::pose_blend(pose, walk, m.h->num_bones, L.move_w);
}

// A one-shot gesture (`anim`, the greeting) over the base pose.
static void overlay_pose(const Overlay& ov, const anim::Binding& b, const dmf::Model& m, dmf::Pose* pose) {
    if (ov.w <= 0.0f || !g_lib) return;
    static dmf::Pose g;
    anim::pose(ov.an, *g_lib, b, m, &g);
    dmf::pose_blend(pose, g, m.h->num_bones, ov.w);
}

static void overlay_tick(Overlay& ov) {
    if (!g_lib) return;
    if (ov.on) {
        anim::tick(ov.an, *g_lib, 1.0f / 60.0f);
        if (ov.an.done) ov.on = false;
    }
    ov.w = approach(ov.w, ov.on ? 1.0f : 0.0f, ov.on ? 0.2f : 0.1f);
}

// ---------------------------------------------------------------------------- cameras
// The look-at point kept inside the map (VS1): near an edge the camera stops and the hero walks off centre
// instead of the view showing the void beyond the map. A map too small for the clamp is centred.
static Vec3 camera_clamp(Vec3 t) {
    const FieldState& f = field_state();
    const float x0 = CAM_CLAMP_SIDE, x1 = (float)f.w - CAM_CLAMP_SIDE;
    const float z0 = CAM_CLAMP_TOP, z1 = (float)f.h - CAM_CLAMP_BOTTOM;
    t.x = x0 < x1 ? (t.x < x0 ? x0 : (t.x > x1 ? x1 : t.x)) : (float)f.w * 0.5f;
    t.z = z0 < z1 ? (t.z < z0 ? z0 : (t.z > z1 ? z1 : t.z)) : (float)f.h * 0.5f;
    return t;
}

static void field_eye_look(Vec3* eye, Vec3* look) {
    const shz_sincos_t pitch = shz_sincosf_deg(CAM_PITCH);               // one FSCA
    *look = g_v.focus + v3(0, CAM_LOOK_H, 0);
    *eye = *look + v3(0, CAM_DIST * pitch.sin, CAM_DIST * pitch.cos);
}

bool field3d_two_shot(float hx, float hz, float nx, float nz, float eye[3], float look[3]) {
    // GDD T3 "dialog shot": the pair's midpoint, seen from the south side of the line perpendicular to the
    // pair (so they stand side by side and the hero is never between the camera and the speaker), at most
    // DLG_YAW degrees off the field camera's heading. The camera therefore swings to the side opposite the
    // hero's offset from the NPC (docs/NEXT_SLICE_DIALOG.md 5.3; a pair on a north-south line gets the full
    // DLG_YAW, turned away from the hero's side, + when they are level).
    float ax = hx - nx, az = hz - nz;
    float px = -az, pz = ax;                                      // a perpendicular...
    if (pz < 0.0f || (pz == 0.0f && px > 0.0f)) { px = -px; pz = -pz; }   // ...on the camera's (south) side
    const float lim = DLG_YAW * (3.14159265f / 180.0f);
    float yaw = (px * px + pz * pz) > 1e-6f ? atan2f(px, pz) : 0.0f;
    if (shz_fabsf(ax) < 0.05f && shz_fabsf(az) > 0.05f) yaw = (ax > 0.0f ? -1.0f : 1.0f) * lim;   // north-south pair
    const bool clamped = yaw >= lim || yaw <= -lim;   // the pair is closer to a north-south line than DLG_YAW allows
    if (yaw > lim) yaw = lim;
    if (yaw < -lim) yaw = -lim;
    const float p = DLG_PITCH * (3.14159265f / 180.0f);
    const Vec3 l = v3((hx + nx) * 0.5f, DLG_LOOK_H, (hz + nz) * 0.5f);
    // The eye must not sit inside a prop (T-CAM): the shot above first, then less turn, then the mirrored
    // turn, then closer; none valid -> the field camera (returns false). A clamped (near north-south) pair
    // tries the mirrored full turn second: a half turn leaves the nearer character covering the other, and
    // both full turns separate them equally (VS1 village: Wren's south neighbour is a house).
    static const float YAW_K[5] = { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f }, YAW_K_NS[5] = { 1.0f, -1.0f, 0.5f, -0.5f, 0.0f };
    static const float DIST_K[3] = { 1.0f, 0.85f, 0.7f };
    const float* yk = clamped ? YAW_K_NS : YAW_K;
    Vec3 e = l;
    bool ok = false;
    for (int di = 0; di < 3 && !ok; ++di)
        for (int yi = 0; yi < 5 && !ok; ++yi) {
            const float y = yaw * yk[yi], d = DLG_DIST * DIST_K[di];
            e = l + v3(sinf(y) * cosf(p) * d, sinf(p) * d, cosf(y) * cosf(p) * d);
            ok = true;
            for (int i = 0; i < g_nstatics && ok; ++i) {
                const float* b = g_static_box[i];
                if (e.x > b[0] - DLG_MARGIN && e.x < b[3] + DLG_MARGIN && e.y > b[1] - DLG_MARGIN && e.y < b[4] + DLG_MARGIN &&
                    e.z > b[2] - DLG_MARGIN && e.z < b[5] + DLG_MARGIN) ok = false;
            }
        }
    eye[0] = e.x; eye[1] = e.y; eye[2] = e.z; look[0] = l.x; look[1] = l.y; look[2] = l.z;
    return ok;
}

// The follow camera eased toward the dialog shot by dialog_cam_weight() (no script camera).
static void field_or_dialog(Vec3* eye, Vec3* look, float* fov) {
    field_eye_look(eye, look);
    *fov = CAM_FOV_Y;
    const float w = dialog_cam_weight();
    if (w > 0.0f && g_v.dlg_ok) {
        *eye = lerp(*eye, g_v.dlg_eye, w);
        *look = lerp(*look, g_v.dlg_look, w);
        *fov += (DLG_FOV - CAM_FOV_Y) * w;
    }
}

static float smooth01(float u) { u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u); return u * u * (3.0f - 2.0f * u); }

// The camera of this tick: the override (title, credits), else the script camera eased between its shots (or back
// to the field), else the follow camera eased toward the dialog shot.
static void current_eye_look(Vec3* eye, Vec3* look, float* fov) {
    if (g_co.on) { *eye = g_co.eye; *look = g_co.look; *fov = g_co.fov; return; }
    if (g_sc.on) {
        Vec3 te = g_sc.to_eye, tl = g_sc.to_look;
        float tf = g_sc.to_fov;
        if (g_sc.to_field) field_or_dialog(&te, &tl, &tf);
        const float w = g_sc.len > 0 ? smooth01((float)g_sc.t / (float)g_sc.len) : 1.0f;
        *eye = lerp(g_sc.from_eye, te, w); *look = lerp(g_sc.from_look, tl, w); *fov = g_sc.from_fov + (tf - g_sc.from_fov) * w;
        return;
    }
    field_or_dialog(eye, look, fov);
}

static void current_camera(Camera* cam) {
    Vec3 eye, look;
    float fov;
    current_eye_look(&eye, &look, &fov);
    camera_look(cam, eye, look, fov);
}

// The point the props' cheap 2D range test centres on: the follow camera's focus, or the ground under the look-at
// point of a scripted / override camera.
static void view_focus(float* fx, float* fz) {
    if (g_co.on || g_sc.on) {
        Vec3 e, l;
        float fov;
        current_eye_look(&e, &l, &fov);
        *fx = l.x; *fz = l.z;
        return;
    }
    *fx = g_v.focus.x; *fz = g_v.focus.z;
}

void field3d_cam_shot(int shot, int ticks) {
    const FieldState& f = field_state();
    Vec3 e, l;
    float fov;
    current_eye_look(&e, &l, &fov);
    g_sc.from_eye = e; g_sc.from_look = l; g_sc.from_fov = fov;
    g_sc.t = 0; g_sc.len = ticks > 0 ? ticks : 0;
    if (shot < 0 || shot >= f.shots.size()) {
        if (!g_sc.on) return;                               // already on the field camera
        g_sc.to_field = true;
        if (g_sc.len == 0) g_sc.on = false;
        else g_sc.on = true;
        return;
    }
    const Shot& sh = f.shots[shot];
    g_sc.on = true; g_sc.to_field = false;
    g_sc.to_eye = v3(sh.eye[0], sh.eye[1], sh.eye[2]); g_sc.to_look = v3(sh.look[0], sh.look[1], sh.look[2]); g_sc.to_fov = sh.fov;
}
bool field3d_cam_busy() { return g_sc.on && g_sc.t < g_sc.len; }
bool field3d_cam_scripted() { return g_sc.on; }
void field3d_cam_release() { g_sc.on = false; }
void field3d_set_camera(const float eye[3], const float look[3], float fov) {
    g_co.on = true; g_co.eye = v3(eye[0], eye[1], eye[2]); g_co.look = v3(look[0], look[1], look[2]); g_co.fov = fov;
}
void field3d_clear_camera() { g_co.on = false; }
void field3d_show_actors(bool hero, bool entities) { g_show_hero = hero; g_show_ents = entities; }

// ---------------------------------------------------------------------------- lifecycle
// ---------------------------------------------------------------------------- character lighting (baked per yaw)
// The pool is split once at init (fixed pointers); a preset change re-bakes in place (slice 3D-6).
static int g_pool_used = 0;
static dmf::Pose g_bake_pose;
static Mat34 g_bake_skin[dmf::MAX_BONES];

static void alloc_look(int k) {
    LookRt& lk = g_look[k];
    if (lk.model < 0 || LOOKS[k].is_static) return;
    for (int j = 0; j < k; ++j)                                   // the same model as an earlier look: share its colours
        if (g_look[j].model == lk.model && !LOOKS[j].is_static && g_look[j].lit[0]) {
            lk.shares = j;
            for (int b = 0; b < ENEMY_YAW_BUCKETS; ++b) lk.lit[b] = g_look[j].lit[b];
            return;
        }
    const dmf::Model& m = models_at(lk.model).m;
    if (g_pool_used + m.h->num_verts * ENEMY_YAW_BUCKETS > ENEMY_YAW_BUCKETS * LIT_POOL_VERTS) {
        eng::dbg_print("field3d: character lighting pool full");
        lk.model = -1;
        return;
    }
    for (int b = 0; b < ENEMY_YAW_BUCKETS; ++b) { lk.lit[b] = g_lit_pool + g_pool_used; g_pool_used += m.h->num_verts; }
}

static void bake_pose(int k) {
    const LookRt& lk = g_look[k];
    const dmf::Model& m = models_at(lk.model).m;
    if (lk.idle_library >= 0 && lk.bound) anim::sample(*g_lib, lk.bind, m, lk.idle_library, 0.0f, &g_bake_pose);
    else dmf::pose_sample(m, lk.idle_embedded, 0.0f, &g_bake_pose);
    dmf::pose_skin(m, g_bake_pose, g_bake_skin);
}

// One yaw bucket of a look under the current preset (the pose's skin matrices in g_bake_skin).
static void bake_bucket(int k, int b) {
    const LookRt& lk = g_look[k];
    const dmf::Model& m = models_at(lk.model).m;
    prelight_posed(m, g_bake_skin, 6.28318531f * (float)b / ENEMY_YAW_BUCKETS, g_light, lk.lit[b]);
    int eye0 = 0, eyes = 0;
    models_glow_range(m, &eye0, &eyes);
    if ((g_lp.colour & 0xFFFFFFu) != 0xFFFFFFu)
        for (int i = 0; i < m.h->num_verts; ++i) lk.lit[b][i] = rgb_mul(lk.lit[b][i], g_lp.colour);
    for (int i = eye0; i < eye0 + eyes; ++i) lk.lit[b][i] = m.verts[i].argb;         // the skeletons' eyes glow (unlit)
}

static void bake_hero_bucket(int b) {
    const dmf::Model& m = models_at(g_knight).m;
    prelight_posed(m, g_bake_skin, 6.28318531f * (float)b / HERO_YAW_BUCKETS, g_light, g_hero_lit[b]);
    if ((g_lp.colour & 0xFFFFFFu) != 0xFFFFFFu)
        for (int i = 0; i < m.h->num_verts; ++i) g_hero_lit[b][i] = rgb_mul(g_hero_lit[b][i], g_lp.colour);
}

static void hero_bake_pose() {
    dmf::pose_sample(models_at(g_knight).m, g_hero_clips.idle, 0.0f, &g_bake_pose);
    dmf::pose_skin(models_at(g_knight).m, g_bake_pose, g_bake_skin);
}

// The re-bake queue: item 0..HERO_YAW_BUCKETS-1 = the hero, then 16 per look (own colours only). A full bake runs
// them all now (map loads, under a fade); after a light lerp they run a few per tick while the characters stay lit
// per frame (no frame over budget: docs/NEXT_SLICE_BARROW.md 5.6).
static int bake_items() { return HERO_YAW_BUCKETS + ELOOK_COUNT * ENEMY_YAW_BUCKETS; }
static bool bake_item(int it) {                                   // false: nothing to bake for this item
    if (it < HERO_YAW_BUCKETS) {
        if (!g_hero_lit_ok) return false;
        hero_bake_pose();                                          // cheap next to the bucket (no state kept between items)
        bake_hero_bucket(it);
        return true;
    }
    it -= HERO_YAW_BUCKETS;
    const int k = it / ENEMY_YAW_BUCKETS, b = it % ENEMY_YAW_BUCKETS;
    const LookRt& lk = g_look[k];
    if (lk.model < 0 || LOOKS[k].is_static || lk.shares >= 0) return false;
    bake_pose(k);
    bake_bucket(k, b);
    return true;
}
static void bake_all() {
    for (int it = 0; it < bake_items(); ++it) bake_item(it);
    g_bake_next = -1;
    g_chars_dynamic = false;
}

// Every static colour set under the current preset (a preset change; lerp steps).
static void relight_statics() { for (int i = 0; i < g_nlit; ++i) light_static(g_lit[i]); }

static void apply_fog() {
    if (g_lp.fog) eng::set_fog(true, g_lp.fog_rgb, g_lp.fog_start, g_lp.fog_end);
    else eng::set_fog(false, 0, 0.0f, 0.0f);
}

void field3d_set_light(int preset) {
    g_lerp.on = false;
    g_lp_id = preset;
    g_lp = light_get(preset);
    g_light = light_of(g_lp);
    if (!g_inited) return;
    relight_statics();
    bake_all();
}

void field3d_light_lerp(int from, int to, int ticks) {
    g_lerp.on = true; g_lerp.from = from; g_lerp.to = to; g_lerp.t = 0; g_lerp.len = ticks > 0 ? ticks : 1;
    g_lp_id = to;
    light_mix(light_get(from), light_get(to), 0.0f, &g_lp);
    g_light = light_of(g_lp);
    g_chars_dynamic = true;
    g_bake_next = -1;
    relight_statics();
}

int field3d_light_preset() { return g_lp_id; }
bool field3d_light_busy() { return g_lerp.on || g_bake_next >= 0; }

// One tick of a lerp: every 8 ticks (and at its end) the blend is applied to the props, the ground, the fog;
// characters are lit per frame meanwhile, then re-baked a few buckets per tick.
static void light_tick() {
    if (g_lerp.on) {
        ++g_lerp.t;
        const bool end = g_lerp.t >= g_lerp.len;
        if (end || g_lerp.t % 8 == 0) {
            light_mix(light_get(g_lerp.from), light_get(g_lerp.to), end ? 1.0f : (float)g_lerp.t / (float)g_lerp.len, &g_lp);
            g_light = light_of(g_lp);
            relight_statics();
        }
        if (end) { g_lerp.on = false; g_bake_next = 0; }
        return;
    }
    if (g_bake_next >= 0) {
        // two yaw buckets per tick (~4k vertices, ~0.25 M SH4 instructions): no frame over budget while it runs
        for (int done = 0; done < 2 && g_bake_next < bake_items();) { if (bake_item(g_bake_next++)) ++done; }
        if (g_bake_next >= bake_items()) { g_bake_next = -1; g_chars_dynamic = false; }
    }
}

void field3d_init() {
    if (!g_inited) {
        g_inited = true;
        if (g_lp.name[0] == '\0') { g_lp = light_get(0); g_light = light_of(g_lp); }
        eng::set_clear_color(CLEAR_RGB);
        g_lib = anim::shared_library();
        g_knight = model_get("knight");
        g_fence = model_get("fence");
        g_tree = model_get("tree");
        g_hero_clips = clips_of(g_knight);
        g_hero_lit_ok = g_knight >= 0 && models_at(g_knight).m.h->num_verts <= MAX_CHAR_VERTS;
        if (g_lib && g_knight >= 0) g_knight_bound = anim::bind(*g_lib, models_at(g_knight).m, &g_knight_bind);
        g_clip_interact = g_lib ? anim::clip_find(*g_lib, "interact") : -1;
        g_clip_awaken = g_lib ? anim::clip_find(*g_lib, "skel_awaken") : -1;
        for (int k = 0; k < ELOOK_COUNT; ++k) {
            LookRt& lk = g_look[k];
            memset(&lk, 0, sizeof lk);
            lk.shares = -1; lk.idle_embedded = -1; lk.idle_library = -1;
            lk.model = model_get(LOOKS[k].model);
            lk.scale = ENEMY_SCALE * LOOKS[k].scale;
            if (lk.model < 0) continue;
            const dmf::Model& m = models_at(lk.model).m;
            lk.top = m.h->bmax[1] * lk.scale;
            if (LOOKS[k].is_static) { lk.clips = clips_of(-1); continue; }   // drawn as a prop (its colours: lit_colors)
            if (m.h->num_verts > MAX_CHAR_VERTS) { lk.model = -1; continue; }
            lk.bound = g_lib && anim::bind(*g_lib, m, &lk.bind);
            if (LOOKS[k].library) {
                lk.clips = g_hero_clips;                          // the Knight's walk stride: the same rig
                lk.clips.idle = g_lib ? anim::clip_find(*g_lib, "idle_a") : -1;
                lk.clips.walk = g_lib ? anim::clip_find(*g_lib, "walk") : -1;
                lk.clips.run = -1;
                if (lk.clips.walk >= 0) lk.clips.walk_len = (float)anim::clip_ticks(*g_lib, lk.clips.walk) / 60.0f;
                if (!lk.bound || lk.clips.idle < 0) { lk.model = -1; eng::dbg_print("field3d: an NPC model could not be bound to the animation library"); continue; }
                lk.idle_library = lk.clips.idle;
            } else {
                lk.clips = clips_of(lk.model);
                lk.idle_embedded = lk.clips.idle;
            }
            alloc_look(k);
        }
        bake_all();
        eng::boot_log("field3d: characters loaded and lit");
        g_shadow_mat = eng::material_create(0, eng::MAT_TRANSLUCENT);
        if (text_texture()) g_bill_mat = eng::material_create(text_texture(), eng::MAT_TRANSLUCENT | eng::MAT_BILINEAR);
    }
    eng::boot_log("field3d: building the static scene (props, fences, trees)");
    build_scene();                      // per map (field_load)
}

void field3d_reset() {
    const FieldState& f = field_state();
    float hx, hz;
    hero_world(f.hero, &hx, &hz);
    int markers[MAX_ENTITIES];
    memcpy(markers, g_v.marker, sizeof markers);
    memset(&g_v, 0, sizeof g_v);
    memcpy(g_v.marker, markers, sizeof markers);
    g_v.focus = camera_clamp(v3(hx, 0, hz - CAM_AHEAD));
    g_v.yaw = dir_yaw(f.hero.dir);
    g_v.prev_x = hx; g_v.prev_z = hz;
    for (int i = 0; i < f.ents.size(); ++i) {
        g_v.ent_yaw[i] = dir_yaw(f.ents[i].w.dir);
        field_entity_pixel(i, &g_v.ent_px[i], &g_v.ent_py[i]);
        g_v.ent[i].idle_t = 0.37f * (float)i;                       // desynchronise the idles
    }
    for (int i = 0; i < g_nstatics; ++i) g_statics[i].alpha = 1.0f;
    g_sc.on = false;                                                // slice 3D-6: a retry / map load ends any script camera
}

void field3d_set_marker(int i, int kind) { if (i >= 0 && i < MAX_ENTITIES) g_v.marker[i] = kind; }
void field3d_show_prompt(bool on) { g_v.prompt = on; }

void field3d_play(int actor, int clip) {
    if (!g_lib || actor < 0 || actor >= MAX_ACTORS_3D || clip < 0 || clip >= g_lib->num_clips) return;
    Overlay& ov = g_v.ov[actor];
    if (!ov.on && ov.w <= 0.0f) anim::animator_init(ov.an, clip);
    anim::play(ov.an, *g_lib, clip, 0.15f, -1);
    if (actor > 0) g_v.woken[actor - 1] = true;                     // slice 3D-6: an `asleep` entity rises
    ov.on = true;
}

void field3d_emote(int actor, int kind) {
    if (actor < 0 || actor >= MAX_ACTORS_3D) return;
    g_v.emote_kind[actor] = kind;
    g_v.emote_t[actor] = 60;
}

void field3d_dialog_begin(int npc) {
    const FieldState& f = field_state();
    g_v.dlg_ok = false;
    if (npc < 0 || npc >= f.ents.size()) return;
    float hx, hz, nx, nz;
    hero_world(f.hero, &hx, &hz);
    entity_world(npc, &nx, &nz);
    float e[3], l[3];
    g_v.dlg_ok = field3d_two_shot(hx, hz, nx, nz, e, l);
    g_v.dlg_eye = v3(e[0], e[1], e[2]);
    g_v.dlg_look = v3(l[0], l[1], l[2]);
    if (g_clip_interact >= 0) field3d_play(1 + npc, g_clip_interact);   // the greeting (GDD 7.2)
}


void field3d_tick() {
    const FieldState& f = field_state();
    const Hero& h = f.hero;
    ++g_v.clock;
    light_tick();                                                   // slice 3D-6: a light lerp, re-bakes
    if (g_sc.on) {
        if (g_sc.t < g_sc.len) ++g_sc.t;
        if (g_sc.to_field && g_sc.t >= g_sc.len) g_sc.on = false;
    }
    float hx, hz;
    hero_world(h, &hx, &hz);
    float dx = hx - g_v.prev_x, dz = hz - g_v.prev_z;
    float d = shz_sqrtf(dx * dx + dz * dz);
    if (d > 1.0f) d = 0.0f;                                   // teleport (reset): no stride
    g_v.prev_x = hx; g_v.prev_z = hz;
    locomotion_tick(g_v.hero, g_hero_clips, d, HERO_SCALE);
    const int talk_npc = dialog_open() ? dialog_npc() : -1;
    float want_yaw = g_v.yaw;
    bool turn = false;
    if (talk_npc >= 0) {                                      // the hero turns to the NPC (GDD 7.2)
        float nx, nz;
        entity_world(talk_npc, &nx, &nz);
        want_yaw = yaw_toward(hx, hz, nx, nz); turn = true;
    } else if (h.vx || h.vy) {                                // face the stick (also against walls)
        // Exact atan2f on purpose (once per tick, not a hot path): a U-turn puts the target exactly
        // pi away, and the last bit of this angle decides which way the knight spins. SH4ZAM's fast
        // approximation (~1e-3 rad) would flip that tie-break on exact reversals.
        want_yaw = atan2f((float)h.vx, (float)h.vy); turn = true;
    }
    if (turn) {
        float t = angle_diff(g_v.yaw, want_yaw) * TURN_RATE;
        if (t > TURN_MAX) t = TURN_MAX;
        if (t < -TURN_MAX) t = -TURN_MAX;
        g_v.yaw = wrap_pi(g_v.yaw + t);
    }
    // camera: lead along the actual motion (blocked = no lead), aim ahead of the hero, keep the view on the map
    const float full = (float)HERO_SPEED_Q8 / (256.0f * (float)SUBPX * (float)TILE_PX);   // tiles per tick at full stick
    const float lx = d > 0.0f ? dx / full * CAM_LEAD : 0.0f, lz = d > 0.0f ? dz / full * CAM_LEAD : 0.0f;
    g_v.lead.x += (lx - g_v.lead.x) * CAM_LEAD_RATE;
    g_v.lead.z += (lz - g_v.lead.z) * CAM_LEAD_RATE;
    Vec3 want = camera_clamp(v3(hx + g_v.lead.x, 0, hz - CAM_AHEAD + g_v.lead.z));
    if (dot(want - g_v.focus, want - g_v.focus) > 8.0f * 8.0f) g_v.focus = want;
    g_v.focus = lerp(g_v.focus, want, CAM_FOLLOW);
    for (int i = 0; i < f.ents.size(); ++i) {
        const Entity& e = f.ents[i];
        float goal = dir_yaw(e.w.dir);
        if (e.kind == EntityKind::Npc && (e.facing_hero || i == talk_npc)) {   // exactly at the hero, not 4-way
            float nx, nz;
            entity_world(i, &nx, &nz);
            goal = yaw_toward(nx, nz, hx, hz);
        }
        g_v.ent_yaw[i] = wrap_pi(g_v.ent_yaw[i] + angle_diff(g_v.ent_yaw[i], goal) * NPC_TURN);
        int px, py;
        field_entity_pixel(i, &px, &py);
        const float ed = shz_sqrtf((float)((px - g_v.ent_px[i]) * (px - g_v.ent_px[i]) + (py - g_v.ent_py[i]) * (py - g_v.ent_py[i]))) / (float)TILE_PX;
        g_v.ent_px[i] = px; g_v.ent_py[i] = py;
        locomotion_tick(g_v.ent[i], g_look[e.look].clips, ed > 1.0f ? 0.0f : ed, ENEMY_SCALE);
    }
    for (int a = 0; a < MAX_ACTORS_3D; ++a) {
        overlay_tick(g_v.ov[a]);
        if (g_v.emote_t[a] > 0) --g_v.emote_t[a];
    }
    // Houses and trees that cover the hero on screen fade out: the prop's projected box overlaps
    // the hero's screen rectangle and the prop is nearer the camera than the hero. In a dialog the
    // speaker counts too (slice 3D-5: the see-through includes the speaker).
    Camera cam;
    current_camera(&cam);
    g_v.cam_eye = cam.eye;
    g_v.cam_look = g_v.focus + v3(0, CAM_LOOK_H, 0);
    struct Who { float x, z, x0, x1, y0, y1, w; bool ok; } who[2];
    int n_who = 0;
    float px_[2] = { hx, 0 }, pz_[2] = { hz, 0 };
    const int npc_seen = dialog_npc();
    if (dialog_cam_weight() > 0.0f && npc_seen >= 0 && npc_seen < f.ents.size()) { entity_world(npc_seen, &px_[1], &pz_[1]); n_who = 2; }
    else n_who = g_show_hero ? 1 : 0;                              // slice 3D-6: no one on the title's backdrop: nothing fades
    for (int k = 0; k < n_who; ++k) {
        Who& w = who[k];
        w.x = px_[k]; w.z = pz_[k];
        const Vec3 feet = mat_point(cam.proj_view, v3(w.x, 0.0f, w.z));
        const Vec3 head = mat_point(cam.proj_view, v3(w.x, 1.9f * HERO_SCALE / 0.8f, w.z));
        w.x0 = 1e9f; w.x1 = -1e9f; w.y0 = 1e9f; w.y1 = -1e9f;
        w.ok = feet.z > cam.near_w && head.z > cam.near_w;
        if (w.ok) {                                                // W > near_w > 0: FSRRA reciprocals
            const float fi = shz_invf_fsrra(feet.z), ti = shz_invf_fsrra(head.z);
            const float fx = cam.cx + feet.x * fi, fy = cam.cy + feet.y * fi;
            const float tx = cam.cx + head.x * ti, ty = cam.cy + head.y * ti;
            const float halfw = 0.35f * (fy - ty);                 // the Knight is about 0.7 as wide as tall
            w.x0 = (fx < tx ? fx : tx) - halfw; w.x1 = (fx > tx ? fx : tx) + halfw;
            w.y0 = ty; w.y1 = fy;
        }
        w.w = feet.z;                                              // W of the feet (depth)
    }
    for (int i = 0; i < g_nstatics; ++i) {
        if (i + 4 < g_nstatics) SHZ_PREFETCH(&g_statics[i + 4].fadeable);   // as in draw_statics
        StaticInst& s = g_statics[i];
        if (!s.fadeable) continue;
        bool hides = false;
        for (int k = 0; k < n_who && !hides; ++k) {
            const Who& w = who[k];
            if (!(w.ok && shz_fabsf(s.x - w.x) < 8.0f && s.z > w.z - 1.0f && s.z - w.z < 12.0f)) continue;
            const dmf::Model& m = models_at(s.model).m;
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, wmin = 1e9f;
            bool behind_cam = false;
            // XMTRX = proj_view * world: the 8 box corners are one FTRV each
            xmtrx_load(cam.proj_view);
            xmtrx_apply(s.world);
            for (int c8 = 0; c8 < 8; ++c8) {
                const Vec3 c = xmtrx_point((c8 & 1) ? m.h->bmax[0] : m.h->bmin[0], (c8 & 2) ? m.h->bmax[1] : m.h->bmin[1],
                                           (c8 & 4) ? m.h->bmax[2] : m.h->bmin[2]);
                if (c.z < cam.near_w) { behind_cam = true; break; }
                const float ci = shz_invf_fsrra(c.z);
                const float sx = cam.cx + c.x * ci, sy = cam.cy + c.y * ci;
                if (sx < x0) x0 = sx;
                if (sx > x1) x1 = sx;
                if (sy < y0) y0 = sy;
                if (sy > y1) y1 = sy;
                if (c.z < wmin) wmin = c.z;
            }
            // nearer than the character (its nearest corner in front of his feet) and overlapping him
            hides = !behind_cam && wmin < w.w - 0.3f && x0 < w.x1 && x1 > w.x0 && y0 < w.y1 && y1 > w.y0 && s.z > w.z + 0.3f;
        }
        // VS1: the closer a hiding prop is to the eye, the fainter (it covers more of the screen); the eye inside
        // its box: not drawn at all
        const float* bx = g_static_box[i];
        const float ddx = bx[0] - cam.eye.x > cam.eye.x - bx[3] ? bx[0] - cam.eye.x : cam.eye.x - bx[3];
        const float ddy = bx[1] - cam.eye.y > cam.eye.y - bx[4] ? bx[1] - cam.eye.y : cam.eye.y - bx[4];
        const float ddz = bx[2] - cam.eye.z > cam.eye.z - bx[5] ? bx[2] - cam.eye.z : cam.eye.z - bx[5];
        const float ex = ddx > 0.0f ? ddx : 0.0f, ey = ddy > 0.0f ? ddy : 0.0f, ez = ddz > 0.0f ? ddz : 0.0f;
        const float dist2 = ex * ex + ey * ey + ez * ez;
        float target = hides ? FADE_ALPHA / 255.0f : 1.0f;
        if (hides && dist2 < NEAR_FADE_DIST * NEAR_FADE_DIST) {
            const float t = clamp01((shz_sqrtf(dist2) - NEAR_FADE_CLOSE) / (NEAR_FADE_DIST - NEAR_FADE_CLOSE));
            target = (NEAR_FADE_MIN + (FADE_ALPHA - NEAR_FADE_MIN) * t) / 255.0f;
        }
        if (dist2 <= 0.0f) target = 0.0f;
        s.alpha = approach(s.alpha, target, FADE_RATE);
    }
}

bool field3d_tune(const char* name, float value) {
    if (strcmp(name, "strip_cull") == 0) { set_strip_culling(value != 0.0f); return true; }
    if (strcmp(name, "near_strips") == 0) { set_near_strips(value != 0.0f); return true; }
    for (const Tune& t : TUNES)
        if (strcmp(t.name, name) == 0) { *t.value = value; return true; }
    return false;
}

Field3dInfo field3d_info() {
    Field3dInfo i;
    memset(&i, 0, sizeof i);
    i.knight_ok = g_knight >= 0; i.cube_ok = g_look[ELOOK_SLIME].model >= 0; i.sphere_ok = g_look[ELOOK_GUARD].model >= 0;
    i.cam_x = g_v.focus.x; i.cam_z = g_v.focus.z; i.hero_yaw = g_v.yaw; i.walk_weight = g_v.hero.move_w;
    i.npc_ok = g_look[ELOOK_MAGE].model >= 0 && g_look[ELOOK_SMITH].model >= 0;
    i.dlg_cam_ok = g_v.dlg_ok;
    Camera cam;
    current_camera(&cam);
    i.eye[0] = cam.eye.x; i.eye[1] = cam.eye.y; i.eye[2] = cam.eye.z;
    Vec3 e, l;
    field_eye_look(&e, &l);
    const float w = dialog_cam_weight();
    if (w > 0.0f && g_v.dlg_ok) l = lerp(l, g_v.dlg_look, w);
    i.look[0] = l.x; i.look[1] = l.y; i.look[2] = l.z;
    i.markers = g_v.n_markers; i.emotes = g_v.n_emotes;
    i.prompt = g_v.prompt && !dialog_open() ? field_talk_target() : -1;
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
        if (d.y < -1e-4f) p = cam.eye + d * shz_divf(-cam.eye.y, d.y);
        else p = cam.eye + normalize(v3(d.x, 0, d.z)) * GROUND_FAR;      // horizon in view
        float hx = p.x - cam.eye.x, hz = p.z - cam.eye.z, hd = shz_sqrtf(hx * hx + hz * hz);
        if (hd > GROUND_FAR) {
            const float clamp = GROUND_FAR * shz_invf_fsrra(hd);
            p.x = cam.eye.x + hx * clamp; p.z = cam.eye.z + hz * clamp;
        }
        if (p.x < *x0) *x0 = p.x;
        if (p.x > *x1) *x1 = p.x;
        if (p.z < *z0) *z0 = p.z;
        if (p.z > *z1) *z1 = p.z;
    }
}

// The ground is a grid over the map plus BORDER_TILES around it. Grid points are transformed once (two rows of
// them are live at a time). The ground texture covers the map continuously (VS1 ground), so a row of quads in
// front of the near plane goes out as ONE strip (2 vertices per tile instead of 4 per quad: the slice-3D-2 atlas
// needed a quad per tile); quads off screen end a strip, quads crossing the near plane go through the clipper.
// Border tiles (outside the map) sample the texture's padding, which holds the outer ground colour.
const int MAX_GRID_W = MAX_MAP_W + 2 * BORDER_TILES + 2;
struct GridPt { float x, y, z; float u, v; bool front; Vec3 p; };   // u, v: the map's texture coordinates

// One row of grid points as an XMTRX batch: XMTRX = cam.screen (viewport folded in), one FTRV and
// one FSRRA per point. The screen position of a point behind the near plane is never used (its
// quads go through the clipper, from g.p). draw_ground's clipper calls reload XMTRX, so each row
// loads it again.
static void ground_row(const Camera& cam, int z, int x0, int x1, GridPt* row) {
    xmtrx_load(cam.screen);
    const float su = (float)g_gv.tpt / (float)g_gv.tw, sv = (float)g_gv.tpt / (float)g_gv.th, m = (float)g_gv.margin;
    for (int x = x0; x <= x1; ++x) {
        GridPt& g = row[x - x0];
        g.p = v3((float)x, 0.0f, (float)z);
        const Vec3 s = xmtrx_point(g.p);                          // (X', Y', W)
        g.front = s.z >= cam.near_w;
        const float inv = shz_invf_fsrra(g.front ? s.z : cam.near_w);
        g.x = s.x * inv; g.y = s.y * inv; g.z = inv;
        g.u = ((float)x + m) * su; g.v = ((float)z + m) * sv;
    }
}

static void draw_ground(const Camera& cam) {
    const FieldState& f = field_state();
    if (!g_gv.mat) return;
    float fx0, fz0, fx1, fz1;
    ground_bounds(cam, &fx0, &fz0, &fx1, &fz1);
    int x0 = (int)shz_floorf(fx0), z0 = (int)shz_floorf(fz0), x1 = (int)shz_ceilf(fx1), z1 = (int)shz_ceilf(fz1);
    if (x0 < -BORDER_TILES) x0 = -BORDER_TILES;
    if (z0 < -BORDER_TILES) z0 = -BORDER_TILES;
    if (x1 > f.w + BORDER_TILES) x1 = f.w + BORDER_TILES;
    if (z1 > f.h + BORDER_TILES) z1 = f.h + BORDER_TILES;
    if (x1 <= x0 || z1 <= z0 || x1 - x0 + 1 > MAX_GRID_W) return;
    eng::Vert3* out = eng::vtx_begin((x1 - x0) * (z1 - z0) * 5);   // bound: a near-clipped quad has up to 5 corners
    if (!out) return;
    const uint32_t tint = 0xFF000000u | g_lp.tint;                 // slice 3D-6: the light preset's tint (day: white)
    static GridPt rows[2][MAX_GRID_W];
    GridPt* top = rows[0];
    GridPt* bot = rows[1];
    ground_row(cam, z0, x0, x1, top);
    int used = 0;
    // beyond the texture's area (the map and its margin): every corner samples the middle of the padding (the
    // far corner texel if the area fills the texture), so the border is one flat colour and a strip breaks there
    const int M = g_gv.margin, aw = (f.w + 2 * M) * g_gv.tpt, ah = (f.h + 2 * M) * g_gv.tpt;
    const float pu = ((float)aw + (float)(g_gv.tw - aw) * 0.5f) / (float)g_gv.tw;
    const float pv = ((float)ah + (float)(g_gv.th - ah) * 0.5f) / (float)g_gv.th;
    auto put = [&](const GridPt& g, uint32_t flags, bool in) {
        eng::Vert3& o = out[used++];
        shz_dcache_alloc_line(&o);      // whole 32-byte line written below (no fill from RAM)
        o.flags = flags;
        o.x = g.x; o.y = g.y; o.z = g.z;
        o.u = in ? g.u : pu; o.v = in ? g.v : pv;
        o.argb = tint; o.oargb = 0;
    };
    for (int z = z0; z < z1; ++z) {
        ground_row(cam, z + 1, x0, x1, bot);
        const bool zin = z >= -M && z < f.h + M;
        int open = -1;                       // the open strip's side (1 map, 0 border), -1 = none
        for (int x = x0; x < x1; ++x) {
            const GridPt& a = top[x - x0];       // (x,   z)
            const GridPt& b = bot[x - x0];       // (x,   z+1)
            const GridPt& c = bot[x + 1 - x0];   // (x+1, z+1)
            const GridPt& d = top[x + 1 - x0];   // (x+1, z)
            const bool in = zin && x >= -M && x < f.w + M;
            const bool all_front = a.front && b.front && c.front && d.front;
            bool fast = false;
            if (all_front) {                     // off-screen? (the bounds are an AABB of a trapezoid)
                // plain compares: on the SH4, fminf/fmaxf are NaN-aware library calls (~200 instr each)
                fast = !((a.x < 0.0f && b.x < 0.0f && c.x < 0.0f && d.x < 0.0f) ||
                         (a.x > 640.0f && b.x > 640.0f && c.x > 640.0f && d.x > 640.0f) ||
                         (a.y < 0.0f && b.y < 0.0f && c.y < 0.0f && d.y < 0.0f) ||
                         (a.y > 480.0f && b.y > 480.0f && c.y > 480.0f && d.y > 480.0f));
            }
            if (open >= 0 && (!fast || open != (int)in)) { out[used - 1].flags = eng::VERT_EOL; open = -1; }
            if (fast) {
                // counter-clockwise seen from above: a(x,z) b(x,z+1) d(x+1,z) c(x+1,z+1), then on along the row
                if (open < 0) { put(a, eng::VERT, in); put(b, eng::VERT, in); open = (int)in; }
                put(d, eng::VERT, in); put(c, eng::VERT, in);
            } else if (!all_front && (a.front || b.front || c.front || d.front)) {
                const float u0 = in ? a.u : pu, u1 = in ? c.u : pu, v0 = in ? a.v : pv, v1 = in ? c.v : pv;
                PolyVert pq[4] = { { a.p, u0, v0, tint }, { b.p, u0, v1, tint },
                                   { c.p, u1, v1, tint }, { d.p, u1, v0, tint } };
                used += emit_poly(cam, pq, 4, out + used);
            }
        }
        if (open >= 0) out[used - 1].flags = eng::VERT_EOL;
        GridPt* tmp = top; top = bot; bot = tmp;
    }
    eng::vtx_end(g_gv.mat, used);
}

// ---------------------------------------------------------------------------- draw
static void draw_statics(const Camera& cam) {
    float fx, fz;
    view_focus(&fx, &fz);
    for (int i = 0; i < g_nstatics; ++i) {
        // Every instance is visited each frame (~180 x 76 bytes, more than a third of the operand
        // cache, evicted by the frame's vertex traffic): request the one four ahead (PREF), as most
        // iterations end at the range test below after a few instructions.
        if (i + 4 < g_nstatics) SHZ_PREFETCH(&g_statics[i + 4].x);
        const StaticInst& s = g_statics[i];
        if (shz_fabsf(s.x - fx) > DRAW_RANGE_X || shz_fabsf(s.z - fz) > DRAW_RANGE_Z) continue;
        const LoadedModel& lm = models_at(s.model);
        const uint32_t* colors = s.colors;
        if (s.alpha < ALPHA_SKIP) continue;           // a faint ghost or the eye inside it: skip (saves its cost)
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


// ---------------------------------------------------------------------------- billboards (5.4)
static TextIcon marker_icon(int kind) { return kind == 0 ? TI_MARKER_QUEST : TI_MARKER_TALK; }
static TextIcon emote_icon(int kind) {
    static const TextIcon E[5] = { TI_EMOTE_EXCL, TI_EMOTE_QUESTION, TI_EMOTE_DOTS, TI_EMOTE_SWEAT, TI_EMOTE_ANGER };
    return (kind >= 0 && kind < 5) ? E[kind] : TI_EMOTE_EXCL;
}

// World position of an actor's head top (0 = Rowan, 1 + i = entity i) and whether it is on the field.
static bool actor_head(int a, Vec3* p) {
    const FieldState& f = field_state();
    if (a == 0) {
        float x, z;
        hero_world(f.hero, &x, &z);
        *p = v3(x, 1.9f * HERO_SCALE / 0.8f, z);
        return true;
    }
    const int i = a - 1;
    if (i < 0 || i >= f.ents.size() || !f.ents[i].alive || f.ents[i].hidden) return false;
    float x, z;
    entity_world(i, &x, &z);
    const float top = g_look[f.ents[i].look].top;
    *p = v3(x, top > 0.5f ? top : 1.9f, z);
    return true;
}

static void draw_billboards_all(const Camera& cam) {
    if (!g_bill_mat || !g_show_ents) return;                       // slice 3D-6: the title's backdrop shows no one
    const FieldState& f = field_state();
    Billboard bb[MAX_ACTORS_3D + MAX_ENTITIES];
    int n = 0, nm = 0, ne = 0;
    const float bob = 0.06f * shz_sinf(6.28318531f * (float)(g_v.clock % 48) / 48.0f);   // +-0.06 over 48 ticks
    const bool talking = dialog_open();
    const int prompt_npc = g_v.prompt ? field_talk_target() : -1;
    for (int i = 0; i < f.ents.size() && !talking; ++i) {            // markers: hidden in dialog and under the prompt
        if (g_v.marker[i] < 0 || i == prompt_npc) continue;
        Vec3 p;
        if (!actor_head(1 + i, &p)) continue;
        Billboard& b = bb[n++];
        b.p = p + v3(0, MARK_LIFT + bob, 0);
        b.w = b.h = MARK_SIZE; b.rot = 0; b.argb = 0xFFFFFFFFu; b.z_bias = 0;
        text_icon_uv(marker_icon(g_v.marker[i]), &b.u0, &b.v0, &b.u1, &b.v1);
        ++nm;
    }
    for (int a = 0; a < MAX_ACTORS_3D; ++a) {                         // emotes (60 ticks, a quick pop)
        if (g_v.emote_t[a] <= 0) continue;
        Vec3 p;
        if (!actor_head(a, &p)) continue;
        const int age = 60 - g_v.emote_t[a];
        const float pop = age < 6 ? 0.5f + 0.5f * (float)age / 6.0f : 1.0f;
        const float fade = g_v.emote_t[a] < 8 ? (float)g_v.emote_t[a] / 8.0f : 1.0f;
        Billboard& b = bb[n++];
        b.p = p + v3(0, MARK_LIFT + bob, 0);
        b.w = b.h = MARK_SIZE * pop; b.rot = 0; b.z_bias = 0;
        b.argb = ((uint32_t)(fade * 255.0f) << 24) | 0x00FFFFFFu;
        text_icon_uv(emote_icon(g_v.emote_kind[a]), &b.u0, &b.v0, &b.u1, &b.v1);
        ++ne;
    }
    if (n) draw_billboards(cam, g_bill_mat, bb, n);
    g_v.n_markers = nm; g_v.n_emotes = ne;
}

// 'A Talk' over the NPC the prompt points at (GDD 7.2), in the UI font (uitext.h); z 2.3 (above the 3D scene).
static void draw_prompt(const Camera& cam) {
    const int i = g_v.prompt && !dialog_open() ? field_talk_target() : -1;
    Vec3 p;
    if (i < 0 || !actor_head(1 + i, &p)) return;
    float sx, sy;
    if (!project(cam, p + v3(0, MARK_LIFT, 0), &sx, &sy)) return;
    const char* label = "TALK";
    const float tw = ui_text_width(UF_S, label);
    const float w = 4.0f + 22.0f + 5.0f + tw + 10.0f, h = 28.0f;
    const float x = (float)(int)(sx - w * 0.5f), y = (float)(int)(sy - h * 0.5f);
    eng::gfx_rect_tr(x, y, w, h, 0xB0000000u, 2.3f);
    ui_icon(UI_PAD_A, x + 4.0f + 11.0f, y + h * 0.5f, 22.0f, 0xFFFFFFFFu, 2.35f);
    ui_text(UF_S, x + 4.0f + 22.0f + 5.0f, y + 7.0f, label, 0xFFFFFFFFu, 2.4f);
}

// Slice 3D-6: the map's glows (the open exit, the boss ring, the barrow's rune ring): additive decals from the
// battle's FX atlas, pulsing over 90 ticks.
static void draw_glows(const Camera& cam) {
    const FieldState& f = field_state();
    GroundQuad q[MAX_GLOWS * 2];
    int n = 0;
    eng::Material mat = 0;
    float uvg[4], uvr[4];
    if (!battle3d_fx(0, &mat, uvg) || !battle3d_fx(1, &mat, uvr)) return;
    const float pulse = 0.75f + 0.25f * shz_sinf(6.28318531f * (float)(g_v.clock % 90) / 90.0f);
    for (int i = 0; i < f.glows.size(); ++i) {
        const Glow& g = f.glows[i];
        if (!g.on) continue;
        const uint32_t a = (uint32_t)(pulse * 255.0f);
        GroundQuad& d = q[n++];
        d.c = v3(g.x, 0.03f, g.z); d.half = g.r; d.rot = 0.0f;
        const float* uv = g.ring ? uvr : uvg;
        d.u0 = uv[0]; d.v0 = uv[1]; d.u1 = uv[2]; d.v1 = uv[3];
        d.argb = a << 24 | (g.rgb & 0xFFFFFFu);
        if (g.ring) {                                                  // a soft glow under the ring
            GroundQuad& e = q[n++];
            e = d; e.half = g.r * 0.85f; e.c.y = 0.02f;
            e.u0 = uvg[0]; e.v0 = uvg[1]; e.u1 = uvg[2]; e.v1 = uvg[3];
            e.argb = (a / 2) << 24 | (g.rgb & 0xFFFFFFu);
        }
    }
    if (n) draw_ground_quads(cam, mat, q, n);
}

void field3d_draw() {
    const FieldState& f = field_state();
    stats_reset();
    // the battle sets its sky colour; restore ours: the preset's, or the outer ground under its tint (day: as before)
    eng::set_clear_color(g_lp.clear_ground ? rgb_mul(g_gv.mat ? g_gv.outer_rgb : CLEAR_RGB, g_lp.tint) : g_lp.clear);
    apply_fog();                                                   // slice 3D-6 (eng::set_fog is cheap when unchanged)
    Camera cam;
    current_camera(&cam);
    const Light light = g_light;
    const uint32_t dyn_tint = 0xFF000000u | g_lp.colour;            // characters lit per frame (a light lerp)

    // Characters first, scenery last: if the frame's vertex arena ever fills up, it is props that
    // drop out, never the hero (the z-buffer makes the order irrelevant to the picture).
    draw_ground(cam);

    // entities: skeletons (their field clips), Wren and Brom (the shared library), a gesture on top
    static dmf::Pose pose;
    static Mat34 skin[dmf::MAX_BONES];
    cel_hull_next(cel().chars >= 0.5f && cel().chars < 1.5f, cel().props >= 0.5f);   // cel prototype (CEL_SHADING_STUDY.md)
    for (int i = 0; i < f.ents.size() && g_show_ents; ++i) {
        const Entity& e = f.ents[i];
        if (!e.alive || e.hidden) continue;
        const LookRt& lk = g_look[e.look];
        float x, z;
        entity_world(i, &x, &z);
        if (lk.model >= 0 && LOOKS[e.look].is_static) {            // slice 3D-6: the waystone (a prop that talks)
            const Mat34 world = mat_trs_y(v3(x, 0, z), dir_yaw(e.home_dir), lk.scale, lk.scale, lk.scale);   // a stone never turns
            const uint32_t* cols = lit_colors(lk.model, dir_yaw(e.home_dir), world);
            if (cols) draw_static(cam, models_at(lk.model).gfx, world, cols);
            draw_blob_shadow(cam, g_shadow_mat, v3(x, 0, z), 0.5f * lk.scale / 0.66f, (int)SHADOW_ALPHA);
            continue;
        }
        if (lk.model >= 0) {
            const dmf::Model& m = models_at(lk.model).m;
            if (e.asleep && !g_v.woken[i] && lk.bound && g_clip_awaken >= 0) {
                anim::sample(*g_lib, lk.bind, m, g_clip_awaken, 0.0f, &pose);   // lying in the ring until the intro
            } else {
                if (LOOKS[e.look].library) library_pose(lk, m, g_v.ent[i], &pose);
                else locomotion_pose(m, lk.clips, g_v.ent[i], &pose);
                if (lk.bound) overlay_pose(g_v.ov[1 + i], lk.bind, m, &pose);
            }
            dmf::pose_skin(m, pose, skin);
            const int bucket = yaw_bucket(g_v.ent_yaw[i], ENEMY_YAW_BUCKETS);
            draw_model(cam, light, models_at(lk.model).gfx, mat_trs_y(v3(x, 0, z), g_v.ent_yaw[i], lk.scale, lk.scale, lk.scale), skin,
                       g_chars_dynamic ? dyn_tint : 0xFFFFFFFFu, g_chars_dynamic ? nullptr : lk.lit[bucket]);
        }
        draw_blob_shadow(cam, g_shadow_mat, v3(x, 0, z), 0.55f * lk.scale / 0.8f, (int)SHADOW_ALPHA);
    }

    // hero
    float hx, hz;
    hero_world(f.hero, &hx, &hz);
    cel_hull_next(cel().chars >= 0.5f, cel().props >= 0.5f);                  // the hero: chars 1 (all) or 2 (hero only)
    if (g_knight >= 0 && g_show_hero) {
        const dmf::Model& m = models_at(g_knight).m;
        locomotion_pose(m, g_hero_clips, g_v.hero, &pose);
        if (g_knight_bound) overlay_pose(g_v.ov[0], g_knight_bind, m, &pose);
        dmf::pose_skin(m, pose, skin);
        const uint32_t* lit = (g_hero_lit_ok && HERO_DYN_LIGHT < 0.5f && !g_chars_dynamic) ? g_hero_lit[yaw_bucket(g_v.yaw, HERO_YAW_BUCKETS)] : nullptr;
        draw_model(cam, light, models_at(g_knight).gfx, mat_trs_y(v3(hx, 0, hz), g_v.yaw, HERO_SCALE, HERO_SCALE, HERO_SCALE), skin,
                   lit ? 0xFFFFFFFFu : dyn_tint, lit);
    }
    if (g_show_hero) draw_blob_shadow(cam, g_shadow_mat, v3(hx, 0, hz), 0.62f * HERO_SCALE / 0.8f, (int)SHADOW_ALPHA);

    cel_hull_next(false, cel().props >= 0.5f);
    draw_statics(cam);
    cel_hull_next(false, false);
    draw_glows(cam);
    draw_billboards_all(cam);
    draw_prompt(cam);
    if (g_knight < 0) ui_text(UF_S, 20, 440, "HERO MODEL MISSING  SEE SERIAL LOG", 0xFFFFCD75, 7.5f);
}

void field3d_draw_hud(int hero_hp, int hero_hp_max, int potions, bool token, const char* objective, bool show) {
    if (!show) return;
    // hero HP (top right; the UI font, docs/UI_ART.md)
    char hud[24];
    snprintf(hud, sizeof hud, "HP %d/%d", hero_hp, hero_hp_max);
    eng::gfx_rect_tr(500, 12, 128, 28, 0xA0000000, 2.0f);
    ui_text(UF_S, 510, 19, "HP", 0xFFFFCD75, 2.2f);
    ui_text(UF_S, 618, 19, hud + 3, hero_hp * 4 > hero_hp_max ? 0xFFF4F4F4 : 0xFFEF7D57, 2.2f, 1.0f, UA_RIGHT);
    // the inventory under it (slice 3D-5; GDD 1.2: potions, plus the quest's token): the potion icon and the
    // count (greyed at 0), Wren's token once given
    snprintf(hud, sizeof hud, "X%d", potions);
    eng::gfx_rect_tr(500, 42, 128, 26, 0xA0000000, 2.0f);
    ui_icon(UI_POTION_S, 516, 55, 24.0f, potions > 0 ? 0xFFFFFFFFu : 0x90808080u, 2.1f);
    ui_text(UF_S, 532, 48, hud, potions > 0 ? 0xFFF0A0A0 : 0xFF808080, 2.2f);
    if (token) ui_icon(UI_TOKEN, 610, 55, 24.0f, 0xFFFFFFFFu, 2.1f);
    // the objective (top left; GDD 10.2 / App. B)
    if (objective && objective[0]) {
        const float w = ui_text_width(UF_S, objective) + 24.0f;
        eng::gfx_rect_tr(16, 14, w, 28, 0xA0000000, 2.0f);
        eng::gfx_rect_tr(16, 14, 3, 28, 0xFFE8B848, 2.05f);
        ui_text(UF_S, 28, 21, objective, 0xFFFFF0C8, 2.2f);
    }
}

} // namespace bt
