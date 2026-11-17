// battle3d_draw.cpp - drawing side of the 3D battle: arena, characters + weapons, decals,
// particles, HUD. Reads g_b3 and the rules context; changes nothing but draw-only caches.
#include "battle3d_internal.h"
#include "dmf.h"
#include "font.h"
#include "icons.h"
#include "models.h"
#include "skills.h"
#include "status.h"
#include "targeting.h"
#include "text.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

namespace bt {

using namespace r3d;

static eng::Material g_fx_add = 0, g_fx_alpha = 0, g_white = 0, g_shadow = 0, g_sky = 0;
static float g_fx_uv[FX_CELL_COUNT][4];
// weapons drawn with their holder's texture: [kind][slot] opaque and faded variants
static ModelGfx g_wpn[K_COUNT][2], g_wpn_faded[K_COUNT][2];
static ModelGfx g_flash[K_COUNT];                    // every material -> untextured white
static ModelGfx g_wpn_flash[K_COUNT][2];
static Mat34 g_idle_skin[K_COUNT][dmf::MAX_BONES];   // idle pose, for the baked lighting
const int LIT_BUCKETS = 32;
const int MAX_LIT_VERTS = 2304;     // the Knight with VS1's head: 2,280 vertices
// Baked lighting per view, double buffered: a new yaw bucket is baked into the hidden buffer a slice
// per frame (BAKE_PER_FRAME vertices over all views) while the old one is shown, then swapped.
static uint32_t g_lit[MAX_ACTORS][2][MAX_LIT_VERTS];
struct LitState { int shown, bucket, baking, pos; };   // shown buffer, its bucket, bucket being baked, progress
static LitState g_ls[MAX_ACTORS];
const int BAKE_PER_FRAME = 512;
static int g_bake_budget = 0;
static uint32_t g_white_cols[MAX_LIT_VERTS];
static uint32_t g_scratch[MAX_LIT_VERTS];
static int g_eye_first[K_COUNT], g_eye_count[K_COUNT];   // untextured batch vertex range (glowing eyes)
static float g_ring_angle = 0.0f;                    // command ring rotation (cosmetic)
static int g_ring_home = -1;

static const int FX_CELLS_PX[FX_CELL_COUNT][4] = {   // tools/battle3d/make_fx.py CELLS
    { 0, 0, 64, 64 }, { 64, 0, 64, 64 }, { 0, 64, 32, 32 }, { 32, 64, 32, 32 }, { 64, 64, 32, 32 },
    { 96, 64, 32, 32 }, { 0, 96, 32, 32 }, { 32, 96, 32, 32 }, { 64, 96, 32, 32 }, { 96, 96, 32, 32 },
};

// FX atlas: romdisk/tex/fx.tex = the .dmf texture header + ARGB4444 texels (make_fx.py)
alignas(32) static uint16_t g_fx_texels[128 * 128 + 16];
static eng::Sprite load_fx_texture() {
    static char buf[32 + 128 * 128 * 2 + 4];
    const int n = eng::file_read("/rd/tex/fx.tex", buf, sizeof buf);
    if (n != 32 + 128 * 128 * 2) { eng::dbg_print("battle3d: /rd/tex/fx.tex missing or wrong size"); return 0; }
    dmf::TexHeader h;
    memcpy(&h, buf, sizeof h);
    if (h.w != 128 || h.h != 128 || h.format != dmf::TEX_ARGB4444) { eng::dbg_print("battle3d: fx.tex bad header"); return 0; }
    memcpy(g_fx_texels, buf + 32, 128 * 128 * 2);
    return eng::texture_create(g_fx_texels, 128, 128, dmf::TEX_ARGB4444);
}

static void make_holder_gfx(ModelGfx* out, int wpn_model, eng::Sprite holder_tex, bool faded) {
    memset(out, 0, sizeof *out);
    if (wpn_model < 0) return;
    const LoadedModel& lm = models_at(wpn_model);
    *out = lm.gfx;
    for (int k = 0; k < lm.m.num_mats; ++k) {
        const bool textured = lm.m.mat_flags[k] & dmf::MAT_BILINEAR;          // '@' materials carry the filter flag
        int f = eng::MAT_BILINEAR | (faded ? eng::MAT_TRANSLUCENT | eng::MAT_CULL : 0);
        out->single[k] = out->doubled[k] = eng::material_create(textured ? holder_tex : 0, f);
    }
}

void b3_draw_init() {
    const eng::Sprite fx = load_fx_texture();
    g_fx_add = eng::material_create(fx, eng::MAT_ADDITIVE | eng::MAT_BILINEAR);
    g_fx_alpha = eng::material_create(fx, eng::MAT_TRANSLUCENT | eng::MAT_BILINEAR);
    g_white = eng::material_create(0, 0);
    g_shadow = eng::material_create(0, eng::MAT_TRANSLUCENT);
    g_sky = eng::material_create(0, 0);
    for (int c = 0; c < FX_CELL_COUNT; ++c) {
        const float k = 1.0f / 128.0f;
        g_fx_uv[c][0] = (FX_CELLS_PX[c][0] + 0.5f) * k; g_fx_uv[c][1] = (FX_CELLS_PX[c][1] + 0.5f) * k;
        g_fx_uv[c][2] = (FX_CELLS_PX[c][0] + FX_CELLS_PX[c][2] - 0.5f) * k; g_fx_uv[c][3] = (FX_CELLS_PX[c][1] + FX_CELLS_PX[c][3] - 0.5f) * k;
    }
    for (int i = 0; i < MAX_LIT_VERTS; ++i) g_white_cols[i] = 0xFFFFFFFFu;
    for (int k = 0; k < K_COUNT; ++k) {
        KindRuntime& kr = g_b3.kinds[k];
        g_eye_first[k] = g_eye_count[k] = 0;
        if (!kr.ok) continue;
        LoadedModel& lm = models_at(kr.model);
        models_make_faded(lm);
        const eng::Sprite tex = lm.m.texture;
        for (int s = 0; s < 2; ++s) {
            make_holder_gfx(&g_wpn[k][s], kr.wpn[s], tex, false);
            make_holder_gfx(&g_wpn_faded[k][s], kr.wpn[s], tex, true);
            g_wpn_flash[k][s] = g_wpn[k][s];
            for (int m = 0; m < dmf::MAX_MATERIALS; ++m) g_wpn_flash[k][s].single[m] = g_wpn_flash[k][s].doubled[m] = g_white;
        }
        g_flash[k] = lm.gfx;
        for (int m = 0; m < lm.m.num_mats; ++m) g_flash[k].single[m] = g_flash[k].doubled[m] = g_white;
        // idle pose skin (baked lighting) and the untextured "eyes" batch
        static dmf::Pose pose;
        anim::sample(g_b3.lib, kr.bind, lm.m, anim::clip_find(g_b3.lib, KINDS[k].idle), 0.0f, &pose);
        dmf::pose_skin(lm.m, pose, g_idle_skin[k]);
        models_glow_range(lm.m, &g_eye_first[k], &g_eye_count[k]);
    }
}

// Baked lighting of a view at its current yaw (32 buckets), re-baked only when the bucket changes.
static void bake_slice(View& v, int vi, int bucket, uint32_t* out, int v0, int v1) {
    const KindRuntime& kr = g_b3.kinds[v.kind];
    const dmf::Model& m = models_at(kr.model).m;
    prelight_posed_range(m, g_idle_skin[v.kind], 6.28318531f * (float)bucket / LIT_BUCKETS, g_b3.arena->light, out, v0, v1);
    for (int i = v0; i < v1; ++i) out[i] = arena_light_color_fast(*g_b3.arena, out[i]);
    // the skeletons' eyes glow in their own colour, unlit (VS1); the Warden's burn purple
    for (int i = g_eye_first[v.kind]; i < g_eye_first[v.kind] + g_eye_count[v.kind]; ++i)
        if (i >= v0 && i < v1) out[i] = v.kind == K_WARDEN ? 0xFFD070FFu : m.verts[i].argb;
    (void)vi;
}

static int yaw_bucket_of(float yaw) {
    int b = (int)shz_floorf(yaw * ((float)LIT_BUCKETS / 6.28318531f) + 0.5f) % LIT_BUCKETS;
    return b < 0 ? b + LIT_BUCKETS : b;
}

void b3_prebake() {                                  // battle start (the transition frame): every view at once
    for (int i = 0; i < g_b3.num_views; ++i) {
        View& v = g_b3.views[i];
        const KindRuntime& kr = g_b3.kinds[v.kind];
        if (!kr.ok) continue;
        const int nv = models_at(kr.model).m.h->num_verts;
        if (nv > MAX_LIT_VERTS) continue;
        LitState& L = g_ls[i];
        L.shown = 0; L.bucket = yaw_bucket_of(v.yaw); L.baking = -1; L.pos = 0;
        bake_slice(v, i, L.bucket, g_lit[i][0], 0, nv);
        v.lit_bucket = L.bucket;
    }
}

static const uint32_t* view_lit(View& v, int vi) {
    const KindRuntime& kr = g_b3.kinds[v.kind];
    const dmf::Model& m = models_at(kr.model).m;
    const int nv = m.h->num_verts;
    if (nv > MAX_LIT_VERTS) return nullptr;
    LitState& L = g_ls[vi];
    if (v.lit_bucket < 0) {                             // a view that appeared (raised): bake it whole
        L.shown = 0; L.bucket = yaw_bucket_of(v.yaw); L.baking = -1; L.pos = 0;
        bake_slice(v, vi, L.bucket, g_lit[vi][0], 0, nv);
        v.lit_bucket = L.bucket;
        return g_lit[vi][0];
    }
    const int b = yaw_bucket_of(v.yaw);
    if (b != L.bucket && L.baking != b) { L.baking = b; L.pos = 0; }   // (re)start toward the current bucket
    if (L.baking >= 0 && g_bake_budget > 0) {
        const int n = nv - L.pos < g_bake_budget ? nv - L.pos : g_bake_budget;
        bake_slice(v, vi, L.baking, g_lit[vi][L.shown ^ 1], L.pos, L.pos + n);
        L.pos += n; g_bake_budget -= n;
        if (L.pos >= nv) { L.shown ^= 1; L.bucket = L.baking; L.baking = -1; v.lit_bucket = L.bucket; }
    }
    return g_lit[vi][L.shown];
}

static Mat34 view_world(const View& v) {
    // dying skeletons sink a little as they fade (they crumble into the ground)
    const float sink = v.dying && v.kind != K_HERO ? (1.0f - v.alpha) * 0.25f : 0.0f;
    return mat_trs_y(v.pos - v3(0, sink, 0), v.yaw, v.scale, v.scale, v.scale);
}

static uint32_t with_alpha(uint32_t c, float a) {
    const int ai = (int)(a * 255.0f + 0.5f);
    return ((uint32_t)(ai < 0 ? 0 : (ai > 255 ? 255 : ai)) << 24) | (c & 0xFFFFFFu);
}

static void draw_view(View& v, int vi, const Camera& cam, bool dim) {
    const KindRuntime& kr = g_b3.kinds[v.kind];
    if (!kr.ok || v.gone) return;
    LoadedModel& lm = models_at(kr.model);
    const dmf::Model& m = lm.m;
    static dmf::Pose pose;
    static Mat34 skin[dmf::MAX_BONES], bw[dmf::MAX_BONES];
    anim::pose(v.an, g_b3.lib, kr.bind, m, &pose);
    dmf::pose_skin_world(m, pose, skin, bw);
    const Mat34 world = view_world(v);
    // hand position (projectiles, charge glow): the right hand slot
    static int slot_r = -1, slot_l = -1;
    if (slot_r < 0) {
        for (int i = 0; i < m.h->num_bones; ++i) {
            if (!strncmp(m.bones[i].name, "handslot.r", 16)) slot_r = i;
            if (!strncmp(m.bones[i].name, "handslot.l", 16)) slot_l = i;
        }
    }
    const Mat34 hand_w = slot_r >= 0 ? mat_mul(world, bw[slot_r]) : world;
    v.hand = v3(hand_w.m[0][3], hand_w.m[1][3], hand_w.m[2][3]);
    // culling sphere around the hips (clips move the body: the death slide, dodges)
    const Vec3 hips = mat_point(world, v3(bw[1].m[0][3], bw[1].m[1][3], bw[1].m[2][3]));
    const float radius = 1.6f * v.scale * 1.15f;
    const uint32_t* lit = view_lit(v, vi);
    const bool flash = v.flash > 0 && (v.flash & 2);
    if (flash) {
        draw_model_in(cam, g_b3.arena->light, g_flash[v.kind], world, skin, hips, radius, 0xFFFFFFFFu, g_white_cols);
    } else if (v.alpha < 0.999f && lit) {
        const uint32_t a = (uint32_t)(v.alpha * 255.0f) << 24;
        for (int i = 0; i < m.h->num_verts; ++i) g_scratch[i] = (lit[i] & 0xFFFFFFu) | a;
        draw_model_in(cam, g_b3.arena->light, lm.faded, world, skin, hips, radius, 0xFFFFFFFFu, g_scratch);
    } else if (dim && lit) {
        for (int i = 0; i < m.h->num_verts; ++i) g_scratch[i] = 0xFF000000u | ((lit[i] >> 1) & 0x7F7F7Fu) | ((lit[i] >> 2) & 0x3F3F3Fu);
        draw_model_in(cam, g_b3.arena->light, lm.gfx, world, skin, hips, radius, 0xFFFFFFFFu, g_scratch);
    } else {
        draw_model_in(cam, g_b3.arena->light, lm.gfx, world, skin, hips, radius, 0xFFFFFFFFu, lit);
    }
    // weapons (lit per vertex: a hundred vertices each), tinted by the arena light
    const float* L = g_b3.arena->light_rgb;
    uint32_t tint = 0xFF000000u | ((uint32_t)(L[0] * 255.0f) << 16) | ((uint32_t)(L[1] * 255.0f) << 8) | (uint32_t)(L[2] * 255.0f);
    if (dim) tint = 0xFF000000u | ((tint >> 1) & 0x7F7F7Fu) | ((tint >> 2) & 0x3F3F3Fu);
    const int slots[2] = { slot_r, slot_l };
    const int wv0 = stats().verts_out;
    for (int s = 0; s < 2; ++s) {
        if (kr.wpn[s] < 0 || slots[s] < 0) continue;
        const Mat34 ww = mat_mul(world, bw[slots[s]]);
        if (v.alpha < 0.999f) draw_model(cam, g_b3.arena->light, g_wpn_faded[v.kind][s], ww, nullptr, with_alpha(tint, v.alpha));
        else draw_model(cam, g_b3.arena->light, flash ? g_wpn_flash[v.kind][s] : g_wpn[v.kind][s], ww, nullptr, flash ? 0xFFFFFFFFu : tint);
    }
    g_b3.cost[1] += stats().verts_out - wv0;
    // blob shadow (fades with the body)
    draw_blob_shadow(cam, g_shadow, v3(v.pos.x, 0, v.pos.z), 0.62f * v.scale / 0.8f, (int)(T_SHADOW_ALPHA * v.alpha));
}

// ---------------------------------------------------------------------------- FX
static Billboard bb(Vec3 p, float size, int cell, uint32_t argb, float rot = 0.0f, float zb = 0.0f) {
    Billboard b;
    b.p = p; b.w = b.h = size; b.rot = rot;
    b.u0 = g_fx_uv[cell][0]; b.v0 = g_fx_uv[cell][1]; b.u1 = g_fx_uv[cell][2]; b.v1 = g_fx_uv[cell][3];
    b.argb = argb; b.z_bias = zb;
    return b;
}

static uint32_t lerp_argb(uint32_t a, uint32_t b, float t) {
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        r |= (uint32_t)(x + (y - x) * t) << sh;
    }
    return r;
}

static void draw_particles(const Camera& cam) {
    static Billboard add[MAX_PARTICLES + 48], alp[MAX_PARTICLES + 16];
    int na = 0, nb = 0;
    for (const Particle& p : g_b3.parts) {
        const float t = (float)p.age / (float)p.life;
        const Billboard b = bb(p.p, p.size0 + (p.size1 - p.size0) * t, p.cell, lerp_argb(p.c0, p.c1, t), p.rot, 0.02f);
        if (p.additive) add[na++] = b; else alp[nb++] = b;
    }
    for (const Projectile& p : g_b3.projs) {
        const bool fire = p.kind == PROJ_FIREBALL;
        add[na++] = bb(p.pos, fire ? 1.0f : 1.1f, FX_GLOW, fire ? 0xFFFFA030 : 0xFFC080FF, 0, 0.05f);
        add[na++] = bb(p.pos, fire ? 0.55f : 0.75f, fire ? FX_FLAME : FX_STAR, fire ? 0xFFFFFFE0 : 0xFFF0E0FF, 0.3f * (float)p.t, 0.06f);
    }
    // charging hands; the Warden's eyes flare while he charges
    for (int i = 0; i < g_b3.num_views; ++i) {
        const View& v = g_b3.views[i];
        if (v.charge_shown) {
            const float pulse = 0.8f + 0.2f * shz_sinf(0.3f * (float)g_b3.frame);
            add[na++] = bb(view_head(v) - v3(0, 0.55f * v.scale, 0), 0.9f * pulse, FX_GLOW, 0xFFD070FF, 0, 0.08f);
            add[na++] = bb(v.hand, 0.9f * pulse, FX_GLOW, 0xFFB060FF, 0, 0.05f);
        }
        if (v.charge > 0) add[na++] = bb(v.hand, 0.5f + 0.02f * (float)(g_b3.frame % 8), FX_GLOW, v.charge_kind ? 0xFFC080FF : 0xFFFFB040, 0, 0.05f);
    }
    draw_billboards(cam, g_fx_alpha, alp, nb);
    draw_billboards(cam, g_fx_add, add, na);
}

static GroundQuad ring(Vec3 c, float half, int cell, uint32_t argb, float rot) {
    GroundQuad q;
    q.c = v3(c.x, 0.03f, c.z); q.half = half; q.rot = rot;
    q.u0 = g_fx_uv[cell][0]; q.v0 = g_fx_uv[cell][1]; q.u1 = g_fx_uv[cell][2]; q.v1 = g_fx_uv[cell][3];
    q.argb = argb;
    return q;
}

// ---------------------------------------------------------------------------- HUD
static void hud_bar(float x, float y, float w, float h, float frac, float ghost, uint32_t fill, float z) {
    eng::gfx_rect_tr(x - 1, y - 1, w + 2, h + 2, 0xE0101018, z);
    eng::gfx_rect_tr(x, y, w, h, 0xFF333C57, z + 0.01f);
    if (ghost > frac) eng::gfx_rect_tr(x + w * frac, y, w * (ghost - frac), h, 0xFFF0D0A0, z + 0.015f);
    if (frac > 0) eng::gfx_rect_tr(x, y, w * frac, h, fill, z + 0.02f);
}

static uint32_t hp_color(float f) { return f > 0.5f ? 0xFF38B764 : (f > 0.25f ? 0xFFFFCD75 : 0xFFEF7D57); }

static void draw_text_outline(float x, float y, const char* s, int scale, uint32_t argb, float z, bool centre = true) {
    if (centre) x -= (float)font_width(s, scale) * 0.5f;
    const uint32_t o = argb & 0xFF000000u;
    font_draw(x - scale, y, s, scale, o, false, z - 0.002f);
    font_draw(x + scale, y, s, scale, o, false, z - 0.002f);
    font_draw(x, y - scale, s, scale, o, false, z - 0.002f);
    font_draw(x, y + scale, s, scale, o, false, z - 0.002f);
    font_draw(x, y, s, scale, argb, false, z);
}

static void draw_hud(BattleCtx& ctx, const Camera& cam, bool choosing) {
    // enemy HP bars above the heads
    for (int i = 0; i < g_b3.num_views; ++i) {
        const View& v = g_b3.views[i];
        if (v.kind == K_HERO || !v.present || v.gone || v.asleep || i == g_b3.boss_vi) continue;
        if (v.dying && v.dead_t > v.collapse_t + 10) continue;
        if (v.rise_t > 0) continue;
        float sx, sy;
        if (!project(cam, view_head(v), &sx, &sy)) continue;
        const float frac = (float)v.shown_hp / (float)v.a->hp_max, ghost = v.bar / (float)v.a->hp_max;
        const float w = v.kind == K_WARDEN ? 72.0f : 52.0f;
        hud_bar(sx - w * 0.5f, sy - 6, w, 6, frac, ghost, hp_color(frac), 3.0f);
        // status pips (burning)
        if (v.burn_shown) { eng::gfx_rect_tr(sx + w * 0.5f + 3, sy - 7, 8, 8, 0xFF101018, 3.0f); eng::gfx_rect_tr(sx + w * 0.5f + 4, sy - 6, 6, 6, 0xFFFF8040, 3.01f); }
    }
    // floating numbers and labels
    for (const FloatText3& f : g_b3.texts) {
        const float u = (float)f.age / (float)f.life;
        float sx, sy;
        if (!project(cam, f.p + v3(0, 0.9f * u, 0), &sx, &sy)) continue;
        const float a = u < 0.7f ? 1.0f : 1.0f - (u - 0.7f) / 0.3f;
        const int pop = f.age < 4 ? 1 : 0;                     // a one-scale pop on arrival
        draw_text_outline(sx, sy - 10, f.text, f.scale + pop, with_alpha(f.color, a), 4.0f);
    }
    // hero panel (bottom left)
    for (int i = 0; i < g_b3.num_views; ++i) {
        const View& v = g_b3.views[i];
        if (v.kind != K_HERO) continue;
        const float x = 16, y = 408;
        eng::gfx_rect_tr(x, y, 236, 56, 0xB0101420, 2.0f);
        eng::gfx_rect_tr(x, y, 236, 2, 0xFFFFCD75, 2.01f);
        font_draw(x + 10, y + 8, "HERO", 2, 0xFFFFCD75, true, 2.1f);
        char hp[16];
        snprintf(hp, sizeof hp, "%d/%d", v.shown_hp, v.a->hp_max);
        font_draw(x + 226 - (float)font_width(hp, 2), y + 8, hp, 2, v.shown_hp * 4 > v.a->hp_max ? 0xFFF4F4F4 : 0xFFEF7D57, true, 2.1f);
        const float frac = (float)v.shown_hp / (float)v.a->hp_max;
        hud_bar(x + 10, y + 30, 216, 10, frac, v.bar / (float)v.a->hp_max, hp_color(frac), 2.05f);
        if (v.guard_shown) font_draw(x + 10, y + 44, "GUARD", 1, 0xFF80D0FF, true, 2.1f);
        if (v.burn_shown) font_draw(x + 50, y + 44, "BURN", 1, 0xFFFF8040, true, 2.1f);
        if (rules().potions) {
            char pc[16];
            snprintf(pc, sizeof pc, "POTIONS %d", ctx.potions);
            font_draw(x + 226 - (float)font_width(pc, 1), y + 44, pc, 1, ctx.potions ? 0xFFF0A0A0 : 0xFF808080, true, 2.1f);
        }
    }
    // the boss: name and a wide HP bar at the top (GDD 12.5), a pip at half (the phase change)
    if (g_b3.boss_vi >= 0) {
        const View& b = g_b3.views[g_b3.boss_vi];
        if (!b.gone && !(b.dying && b.dead_t > b.collapse_t + 60)) {
            const float x = 170, y = 18, w = 300;
            eng::gfx_rect_tr(x - 10, y - 6, w + 20, 40, 0xB0100818, 2.0f);
            draw_text_outline(320, y - 1, KINDS[b.kind].display, 2, 0xFFE0B0FF, 2.2f);
            const float frac = (float)b.shown_hp / (float)b.a->hp_max;
            hud_bar(x, y + 18, w, 8, frac, b.bar / (float)b.a->hp_max, g_b3.boss_phase2 ? 0xFFE04060 : 0xFFA060E0, 2.05f);
            eng::gfx_rect_tr(x + w * 0.5f - 1, y + 15, 2, 14, 0xFFFFE0A0, 2.1f);
        }
    }
    // captions: the telegraph and the Warden's line
    if (g_b3.caption != CAP_NONE) {
        const char* text = g_b3.caption == CAP_CHARGE ? "THE WARDEN DRAWS IN THE DARK..." : "YOU WILL NOT TAKE THEM FROM ME.";
        const float a = g_b3.caption_t < 20 ? (float)g_b3.caption_t / 20.0f : 1.0f;
        eng::gfx_rect_tr(40, 352, 560, 40, with_alpha(0xFF100818, 0.75f * a), 6.0f);
        if (g_b3.caption == CAP_PHASE) font_draw(56, 344, "THE BONE WARDEN", 1, with_alpha(0xFFE0B0FF, a), true, 6.1f);
        draw_text_outline(320, 364, text, 2, with_alpha(g_b3.caption == CAP_CHARGE ? 0xFFD090FF : 0xFFF0E0FF, a), 6.1f);
    }
    // the big name card of the intro
    if (g_b3.namecard_t > 0) {
        const int t = 110 - g_b3.namecard_t;
        const float a = t < 15 ? (float)t / 15.0f : (g_b3.namecard_t < 20 ? (float)g_b3.namecard_t / 20.0f : 1.0f);
        eng::gfx_rect_tr(0, 150, 640, 70, with_alpha(0xFF080410, 0.7f * a), 7.0f);
        draw_text_outline(320, 168, "THE BONE WARDEN", 4, with_alpha(0xFFE0B0FF, a), 7.1f);
        eng::gfx_rect_tr(160, 206, 320 * (t < 30 ? (float)t / 30.0f : 1.0f), 2, with_alpha(0xFFA060E0, a), 7.05f);
    }
    if (!choosing || !ctx.active) return;
    // ---- command selection (the rules' UI state, drawn in 3D)
    View* hv = view_of(ctx.active);
    if (!hv) return;
    ActorList enemies = alive_enemies(ctx, *ctx.active);
    const Ui& ui = ctx.ui;
    static const Icon RING_ICON[4] = { ICON_SWORD, ICON_FLAME, ICON_SHIELD, ICON_POTION };
    static const char* const RING_NAME[4] = { "ATTACK", "FIRE", "GUARD", "POTION" };
    const int ncmd = ui_command_count();
    const float bob = shz_sinf((float)ui.bob_ticks * 0.12f) * 4.0f;
    if (ui.state == UiState::Home) {
        // three icons on a ring around the hero's chest; the selected one swings to the front
        const Vec3 c = view_chest(*hv) + v3(0, 0.35f, 0);
        const float step = 6.28318531f / (float)ncmd;
        for (int k = 0; k < ncmd; ++k) {
            const float a = g_ring_angle + step * (float)k;
            const shz_sincos_t sc = shz_sincosf(a);
            const Vec3 p = c + v3(sc.sin * 0.85f, 0.0f, sc.cos * 0.85f);
            float sx, sy;
            if (!project(cam, p, &sx, &sy)) continue;
            const bool sel = k == ui.home_i;
            const float depth = 0.5f + 0.5f * sc.cos;                // 1 = front
            const bool empty = k == 3 && ctx.potions <= 0;        // no potions left: greyed
            const bool locked = k == 1 && fire_locked(ctx);       // slice 3D-5: Fire not learned yet - greyed + lock
            uint32_t col = sel ? 0xFFFFFFFFu : with_alpha(0xFFC0C0C0u, 0.45f + 0.4f * depth);
            if (empty || locked) col = with_alpha(0xFF606060u, sel ? 0.9f : 0.5f);
            icon_draw_tint((Icon)RING_ICON[k], sx, sy + (sel ? bob : 0.0f), sel ? 48.0f : 32.0f, col, sel ? 5.1f : 5.0f);
            if (locked) text_icon_draw(TI_LOCK, sx + (sel ? 12.0f : 8.0f), sy + (sel ? bob : 0.0f) + (sel ? 12.0f : 8.0f), 1.0f,
                                       sel ? 0xFFFFFFFFu : with_alpha(0xFFFFFFFFu, 0.45f + 0.4f * depth), sel ? 5.15f : 5.05f);
            if (k == 3 && sel) {
                char n[8];
                snprintf(n, sizeof n, "X%d", ctx.potions);
                draw_text_outline(sx + 20, sy + 10, n, 2, empty ? 0xFF909090 : 0xFFF4F4F4, 5.2f, false);
            }
        }
        float sx, sy;
        if (project(cam, view_head(*hv) + v3(0, 0.35f, 0), &sx, &sy))
            draw_text_outline(sx, sy - 24, RING_NAME[ui.home_i], 2, (ui.home_i == 1 && fire_locked(ctx)) ? 0xFF909090 : 0xFFFFCD75, 5.2f);
        static const UiPrompt P[] = { { ICON_PAD_A, ICON_COUNT, "SELECT" }, { ICON_PAD_LEFT, ICON_PAD_RIGHT, "CHOOSE" },
                                      { ICON_PAD_Y, ICON_COUNT, "GUARD" } };
        ui_draw_prompts(P, 3);
        return;
    }
    if (ui.state == UiState::Target) {
        if (enemies.empty()) return;
        const View* tv = view_of(enemies[clampv(ui.target_i, 0, enemies.size() - 1)]);
        if (!tv) return;
        float sx, sy;
        if (project(cam, view_head(*tv) + v3(0, 0.25f, 0), &sx, &sy)) {
            draw_text_outline(sx, sy - 34, KINDS[tv->kind].display, 2, 0xFFFFCD75, 5.2f);
        }
        static const UiPrompt P[] = { { ICON_PAD_A, ICON_COUNT, "ATTACK" }, { ICON_PAD_LEFT, ICON_PAD_RIGHT, "TARGET" },
                                      { ICON_PAD_B, ICON_COUNT, "BACK" } };
        ui_draw_prompts(P, 3);
    } else {
        draw_text_outline(320, 60, "ALL ENEMIES", 2, 0xFFEF7D57, 5.2f);
        static const UiPrompt P[] = { { ICON_PAD_A, ICON_COUNT, "CAST FIRE" }, { ICON_PAD_B, ICON_COUNT, "BACK" } };
        ui_draw_prompts(P, 2);
    }
}

// ---------------------------------------------------------------------------- frame
void battle3d_draw(BattleCtx& ctx) {
    stats_reset();
    g_bake_budget = BAKE_PER_FRAME;
    if (!g_b3.num_views || !g_b3.arena) return;
    const Arena& A = *g_b3.arena;
    eng::set_clear_color(A.sky);
    Camera cam;
    cam_camera(&cam);
    const bool choosing = ctx.state == BattleState::Command && ctx.active && ctx.active->team == Team::Player && !battle3d_busy();
    // command ring angle eases toward the selection (cosmetic, advanced by draws)
    if (choosing && ctx.ui.state == UiState::Home) {
        const float goal = -(6.28318531f / (float)ui_command_count()) * (float)ctx.ui.home_i;
        if (g_ring_home < 0) g_ring_angle = goal;
        g_ring_angle += angle_diff(g_ring_angle, goal) * 0.25f;
        g_ring_home = ctx.ui.home_i;
    } else {
        g_ring_home = -1;
    }

    // horizon band: a quad far behind the arena, sky colour at the top, the horizon colour low
    {
        const uint32_t top = 0xFF000000u | A.sky, low = 0xFF000000u | A.horizon;
        PolyVert q[4] = { { v3(-400, -2, -45), 0, 0, low }, { v3(400, -2, -45), 0, 0, low },
                          { v3(400, 30, -45), 0, 0, top }, { v3(-400, 30, -45), 0, 0, top } };
        eng::Vert3* out = eng::vtx_begin(8);
        if (out) eng::vtx_end(g_sky, emit_poly(cam, q, 4, out));
    }

    // characters first: if the vertex arena ever fills, scenery drops out, not the fighters
    int target_vi = -1;
    ActorList enemies;
    if (choosing) {
        enemies = alive_enemies(ctx, *ctx.active);
        if (ctx.ui.state == UiState::Target && !enemies.empty()) {
            const View* tv = view_of(enemies[clampv(ctx.ui.target_i, 0, enemies.size() - 1)]);
            target_vi = tv ? (int)(tv - g_b3.views) : -1;
        }
    }
    for (int k = 0; k < 5; ++k) g_b3.cost[k] = 0;
    const int c0 = stats().verts_out;
    for (int i = 0; i < g_b3.num_views; ++i) {
        View& v = g_b3.views[i];
        if (!v.present) continue;
        const bool dim = target_vi >= 0 && v.kind != K_HERO && i != target_vi;
        draw_view(v, i, cam, dim);
    }
    g_b3.cost[0] = stats().verts_out - c0 - g_b3.cost[1];
    const int a0 = stats().verts_out;
    arena_draw(A, cam, &g_b3.cost[2]);
    g_b3.cost[3] = stats().verts_out - a0 - g_b3.cost[2];
    const int f0 = stats().verts_out;

    // decals: selection rings, the acting enemy, guard
    GroundQuad rings[MAX_ACTORS + 8];
    int nr = 0;
    const float spin = 0.02f * (float)g_b3.frame;
    if (choosing) {
        if (ctx.ui.state == UiState::Target && target_vi >= 0) {
            rings[nr++] = ring(g_b3.views[target_vi].pos, 0.95f * g_b3.views[target_vi].scale / 0.8f, FX_RUNE, 0xFFFFC850, spin);
        } else if (ctx.ui.state == UiState::TargetAll || (ctx.ui.state == UiState::Home && ctx.ui.home_i == 1 && !fire_locked(ctx))) {
            const bool confirm = ctx.ui.state == UiState::TargetAll;
            for (Actor* e : enemies) {
                const View* ev = view_of(e);
                if (ev) rings[nr++] = ring(ev->pos, (confirm ? 0.95f : 0.75f) * ev->scale / 0.8f, FX_RUNE,
                                           confirm ? 0xFFFF7038 : 0x90FF7038, -spin);
            }
        }
    }
    if (!g_b3.queue.empty() && g_b3.queue[0].started) {
        const Seq& s = g_b3.queue[0];
        const View* u = view_of(s.ev.user);
        if (u && u->kind != K_HERO && s.kind != SEQ_BURN) rings[nr++] = ring(u->pos, 0.85f * u->scale / 0.8f, FX_RING, 0xC0FF3020, 0);
    }
    for (int i = 0; i < g_b3.num_views; ++i) {
        const View& v = g_b3.views[i];
        if (v.guard_shown) rings[nr++] = ring(v.pos, 1.0f * v.scale / 0.8f, FX_RING, v.kind == K_WARDEN ? 0xA0C060FF : 0xA060B0FF, 0);
    }
    if (g_b3.boss_vi >= 0) {                                   // the Warden's rune ring: purple, red in phase 2
        const View& b = g_b3.views[g_b3.boss_vi];
        const float fade = b.dying ? (b.dead_t < 90 ? 1.0f - (float)b.dead_t / 90.0f : 0.0f) : 1.0f;
        if (fade > 0.0f) {
            const uint32_t c = g_b3.boss_phase2 ? 0xFFFF3048 : 0xFFA050FF;
            rings[nr++] = ring(b.home, 1.7f, FX_RUNE, with_alpha(c, 0.85f * fade), 0.008f * (float)g_b3.frame);
            if (g_b3.ring_pulse_t > 0) {
                const float u = 1.0f - (float)g_b3.ring_pulse_t / 40.0f;
                rings[nr++] = ring(b.home, 1.7f + 3.0f * u, FX_RING, with_alpha(c, 1.0f - u), 0);
            }
        }
    }
    draw_ground_quads(cam, g_fx_add, rings, nr);

    // guard bubble and the target arrow (billboards), particles
    {
        Billboard b[MAX_ACTORS * 2 + 2];
        int n = 0;
        for (int i = 0; i < g_b3.num_views; ++i) {
            const View& v = g_b3.views[i];
            if (v.guard_shown) b[n++] = bb(view_chest(v), 2.3f * v.scale / 0.8f, FX_BUBBLE, v.kind == K_WARDEN ? 0x80C080FF : 0x7090C8FF, 0, 0.0f);
        }
        if (target_vi >= 0) {
            const float bob = 0.12f * shz_sinf(0.15f * (float)g_b3.frame);
            b[n++] = bb(view_head(g_b3.views[target_vi]) + v3(0, 0.55f + bob, 0), 0.6f, FX_ARROW, 0xFFFFC850, 0, 0.1f);
        }
        draw_billboards(cam, g_fx_alpha, b, n);
    }
    draw_particles(cam);
    g_b3.cost[4] = stats().verts_out - f0;

    draw_hud(ctx, cam, choosing);
    g_b3.n_enemy_sx = 0;
    for (const Actor& a : ctx.actors) {
        if (a.team != Team::Enemy || a.hp <= 0) continue;
        const View* v = view_of(&a);
        float sx = -1.0f, sy;
        if (v) project(cam, view_chest(*v), &sx, &sy);
        g_b3.enemy_sx[g_b3.n_enemy_sx++] = sx;
    }

    // intro hint, result banner
    if (!g_b3.queue.empty() && g_b3.queue[0].kind == SEQ_INTRO) {
        icon_draw_tint(ICON_PAD_A, 560, 452, 16, 0xFFFFFFFF, 6.0f);
        font_draw(572, 446, "SKIP", 2, 0xFFF4F4F4, true, 6.0f);
    }
    if ((ctx.state == BattleState::Win || ctx.state == BattleState::Lose) && !battle3d_busy()) {
        const bool won = ctx.state == BattleState::Win;
        const int t = ctx.result_ticks;
        const float grow = t < 12 ? (float)t / 12.0f : 1.0f;
        // upper third: the hero's celebration (or fall) stays visible below
        eng::gfx_rect_tr(0, 40 + 44 * (1 - grow), 640, 88 * grow, 0xB0000000, 8.0f);
        if (t >= 6) draw_text_outline(320, 52, won ? "VICTORY!" : "DEFEAT...", 4, won ? 0xFFFFCD75 : 0xFFEF7D57, 8.5f);
        if (t >= 30 && (t / 20) % 2 == 0) draw_text_outline(320, 96, "PRESS A", 2, 0xFFF4F4F4, 8.5f);
    }
}

}
