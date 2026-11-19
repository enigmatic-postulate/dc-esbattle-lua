// render3d.cpp - see render3d.h
//
// Per-model cost on the SH4 (what this loop is shaped for):
//   * one 3x4 "screen * view * world * skin" matrix per bone, built once per frame with SH4ZAM's
//     XMTRX products (FTRV);
//   * vertices come pre-sorted into skin groups, so each group loads its bone matrix into XMTRX
//     once and then streams its vertices: one FTRV per vertex (point transform) and one FSRRA for
//     the perspective divide (W >= near_w > 0 on every path, which FSRRA's 1/|W| needs);
//   * lighting: the light direction is moved into each bone's space once per frame, so a
//     vertex's diffuse term is one or two 3-term dot products with its stored int8 normal (only
//     for models drawn without baked colours);
//   * each distinct vertex is transformed once into a scratch array laid out exactly like the
//     engine's 32-byte vertex, then the strips (already built by the converter) are copied into the
//     engine's vertex arena, one 32-byte cache line per vertex claimed with MOVCA.L
//     (shz_dcache_alloc_line) so the write does not first read the line from RAM;
//   * memory: every buffer these loops stream (model vertices, the transform scratch, the arena) is
//     several times the 16 KiB operand cache, so the loops that index them request the line they
//     will need two vertices later with PREF (SHZ_PREFETCH) - a software pipeline in the strip
//     loops, which read vertices in strip order - and claim whole lines they are about to write
//     with MOVCA.L. tests/sh4zam/shcache.c measures the effect (docs/SLICE_SH4ZAM.md, §8).
// Hardware math is all SH4ZAM (see math3d.h); this file contains no assembly.
#include "render3d.h"
#include <stddef.h>
#include <string.h>
#include <algorithm>

namespace r3d {

static_assert(sizeof(eng::Vert3) == 32, "the strip copy moves one 32-byte block per vertex");

static Stats g_stats;
static bool g_strip_culling = true;
void set_strip_culling(bool on) { g_strip_culling = on; }
static bool g_near_strips = true;
void set_near_strips(bool on) { g_near_strips = on; }
static bool g_hull_chars = false, g_hull_props = false;     // cel prototype: outline the draws that follow
void stats_reset() { memset(&g_stats, 0, sizeof g_stats); g_hull_chars = g_hull_props = false; }
const Stats& stats() { return g_stats; }
void stats_add(int verts_out, int tris) { g_stats.verts_out += verts_out; g_stats.tris += tris; }

Light default_light() {
    Light l;
    l.dir = normalize(v3(-0.35f, 0.85f, 0.40f));   // from the upper left, towards the camera side
    l.ambient = 0.55f;
    l.diffuse = 0.50f;
    return l;
}

void camera_look(Camera* c, Vec3 eye, Vec3 target, float fov_y_deg) {
    c->eye = eye;
    c->view = mat_look_at(eye, target, v3(0, 1, 0));
    c->cx = 320.0f;
    c->cy = 240.0f;
    c->tan_y = shz_tanf_deg(fov_y_deg * 0.5f);
    c->tan_x = c->tan_y * (640.0f / 480.0f);
    c->focal = 240.0f / c->tan_y;
    c->near_w = 0.5f;                          // keeps every z = 1/W <= 2 (HUD sits at z >= 7)
    c->cos_x = shz_inv_sqrtf(1.0f + c->tan_x * c->tan_x);
    c->cos_y = shz_inv_sqrtf(1.0f + c->tan_y * c->tan_y);
    c->sec_x = shz_sqrtf(1.0f + c->tan_x * c->tan_x);
    c->sec_y = shz_sqrtf(1.0f + c->tan_y * c->tan_y);
    // proj_view = diag(f, -f, -1) * view: X = f * x_view, Y = -f * y_view (screen y down), W = -z_view
    shz_xmtrx_init_scale(c->focal, -c->focal, -1.0f);
    xmtrx_apply(c->view);
    xmtrx_store(&c->proj_view);
    // screen = viewport * proj_view (sx = cx + X/W = (X + cx W) / W): the vertex loops then need
    // no viewport offset at all.
    const Mat34 viewport = { { { { 1.0f, 0.0f, c->cx, 0.0f }, { 0.0f, 1.0f, c->cy, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } } } };
    xmtrx_load(viewport);
    xmtrx_apply(c->proj_view);
    xmtrx_store(&c->screen);
}

// ---------------------------------------------------------------------------- models
const int MAX_XV = 4096;
// Transformed vertices, shaped exactly like eng::Vert3, one 32-byte cache line each.
alignas(32) static eng::Vert3 g_xv[MAX_XV];
struct XT { float X, Y, W, ndl; };           // first-bone partial result for two-bone groups
// Two entries per 32-byte cache line: draw_model's first pass claims each line with MOVCA.L
// (shz_dcache_alloc_line) before writing it - both halves are written before either is read, or
// belong to a group whose second pass is already done.
alignas(32) static XT g_xt[MAX_XV];
static_assert(sizeof(XT) == 16, "two XT entries per cache line");

// MOVCA.L for the g_xt line that starts at t (called for every entry; only line starts allocate).
static inline void xt_claim(XT* t) {
    if (!(reinterpret_cast<uintptr_t>(t) & 31)) shz_dcache_alloc_line(t);
}

typedef uint32_t __attribute__((may_alias)) u32a;   // word copies of float data without aliasing UB

// Writes one vertex as a whole 32-byte, 32-byte-aligned cache line (arena slots and g_xv entries):
// SH4ZAM's shz_dcache_alloc_line (MOVCA.L) claims the line without reading it from RAM - every
// vertex buffer here is far larger than the 16 KiB operand cache, so a plain write would first
// fetch the line - then all 8 words are written. u and v are copied as raw words.
static inline void put_vert(eng::Vert3* o, float x, float y, float z, uint32_t u, uint32_t v, uint32_t argb) {
    shz_dcache_alloc_line(o);
    u32a* d = reinterpret_cast<u32a*>(o);
    d[0] = eng::VERT;
    o->x = x; o->y = y; o->z = z;
    d[4] = u; d[5] = v; d[6] = argb; d[7] = 0;
}
static inline uint32_t fbits(const float& f) { return *reinterpret_cast<const u32a*>(&f); }

// ---------------------------------------------------------------------------- cel prototype
// docs/CEL_SHADING_STUDY.md. Defaults: off. The hardware test builds (makefile): CEL=1 = toon lighting + 2 px outlines
// from the decimated hull meshes on every character; CEL=2 = the same on the hero only. No props, no weapons.
#ifndef CEL_DEFAULT
#define CEL_DEFAULT 0
#endif
static CelParams g_cel = { CEL_DEFAULT ? 2.0f : 0.0f, CEL_DEFAULT == 2 ? 2.0f : 1.0f, 0.0f, CEL_DEFAULT ? 1.0f : 0.0f,
                           0.12f, 0.55f, (float)0x140C10, 1.0f, 0.05f, 0.0f };
CelParams& cel() { return g_cel; }
void cel_hull_next(bool characters, bool props) { g_hull_chars = characters; g_hull_props = props; }
float cel_ndl(float ndl) {
    if (g_cel.toon < 0.5f) return ndl;
    return ndl < g_cel.band_lo ? 0.0f : (ndl < g_cel.band_hi ? 0.5f : 1.0f);
}
static eng::Material g_hull_mat = 0;
static eng::Material hull_material() {
    if (!g_hull_mat) g_hull_mat = eng::material_create(0, eng::MAT_CULL_FRONT);   // once, the first time it is needed
    return g_hull_mat;
}
static uint32_t hull_colour() { return 0xFF000000u | ((uint32_t)g_cel.outline_rgb & 0xFFFFFFu); }

// Welded normals into the vertices' pad bytes (load time; the model image is our own static arena memory).
static void weld_normals(const dmf::Model& m) {
    const int nv = m.h->num_verts;
    static int idx[4096];
    static int32_t key[4096][3];
    if (nv > 4096) return;
    dmf::Vertex* vs = const_cast<dmf::Vertex*>(m.verts);
    for (int i = 0; i < nv; ++i) {
        idx[i] = i;
        key[i][0] = (int32_t)(vs[i].x * 2048.0f + (vs[i].x < 0 ? -0.5f : 0.5f));
        key[i][1] = (int32_t)(vs[i].y * 2048.0f + (vs[i].y < 0 ? -0.5f : 0.5f));
        key[i][2] = (int32_t)(vs[i].z * 2048.0f + (vs[i].z < 0 ? -0.5f : 0.5f));
    }
    std::sort(idx, idx + nv, [](int a, int b) {
        if (key[a][0] != key[b][0]) return key[a][0] < key[b][0];
        if (key[a][1] != key[b][1]) return key[a][1] < key[b][1];
        return key[a][2] < key[b][2];
    });
    for (int s0 = 0; s0 < nv;) {
        int s1 = s0 + 1;
        while (s1 < nv && key[idx[s1]][0] == key[idx[s0]][0] && key[idx[s1]][1] == key[idx[s0]][1] && key[idx[s1]][2] == key[idx[s0]][2]) ++s1;
        float n[3] = { 0, 0, 0 };
        for (int k = s0; k < s1; ++k) { n[0] += vs[idx[k]].nx; n[1] += vs[idx[k]].ny; n[2] += vs[idx[k]].nz; }
        const float l2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
        const float inv = l2 > 1e-6f ? 127.0f / sqrtf(l2) : 0.0f;
        for (int k = s0; k < s1; ++k) {
            uint8_t* p = vs[idx[k]].pad;
            p[0] = (uint8_t)(int8_t)(n[0] * inv); p[1] = (uint8_t)(int8_t)(n[1] * inv); p[2] = (uint8_t)(int8_t)(n[2] * inv); p[3] = 0;
        }
        s0 = s1;
    }
}

// The hull's strips: every opaque batch of the model, from g_xv (already holding the pushed-out vertices), into
// one arena batch with the hull material; strips wholly off screen are skipped. Same copy loop as emit_batches.
static bool emit_hull_strips(const dmf::Model& m) {
    int total = 0;
    for (int k = 0; k < m.h->num_batches; ++k)
        if (!(m.batches[k].flags & dmf::BATCH_TRANSLUCENT)) total += (int)(m.batches[k].num_words - m.batches[k].num_strips);
    const eng::Material mat = hull_material();
    if (!total || !mat) return false;
    eng::Vert3* out = eng::vtx_begin(total);
    if (!out) return false;
    eng::Vert3* o = out;
    int tris = 0;
    for (int k = 0; k < m.h->num_batches; ++k) {
        const dmf::Batch& bt = m.batches[k];
        if (bt.flags & dmf::BATCH_TRANSLUCENT) continue;
        const uint16_t* w = m.strips + bt.first_word;
        const uint16_t* end = w + bt.num_words;
        while (w < end) {
            const int len = *w++;
            SHZ_PREFETCH(w + 16);
            int l = 0, r = 0, t = 0, b = 0;
            for (int j = 0; j < len; ++j) {
                const eng::Vert3& x = g_xv[w[j]];
                l += x.x < 0.0f; r += x.x > 640.0f; t += x.y < 0.0f; b += x.y > 480.0f;
            }
            if (l == len || r == len || t == len || b == len) { w += len; continue; }
            tris += len - 2;
            const eng::Vert3* a = &g_xv[w[0]];
            const eng::Vert3* bb = &g_xv[w[1]];
            SHZ_PREFETCH(bb);
            for (int j = 0; j < len; ++j) {
                const eng::Vert3* c = &g_xv[w[j + 2]];
                SHZ_PREFETCH(c);
                const u32a* sv32 = reinterpret_cast<const u32a*>(a);
                u32a* d = reinterpret_cast<u32a*>(o);
                shz_dcache_alloc_line(o);
                d[0] = eng::VERT; d[1] = sv32[1]; d[2] = sv32[2]; d[3] = sv32[3];
                d[4] = sv32[4]; d[5] = sv32[5]; d[6] = sv32[6]; d[7] = 0;
                ++o;
                a = bb; bb = c;
            }
            o[-1].flags = eng::VERT_EOL;
            w += len;
        }
    }
    const int used = (int)(o - out);
    eng::vtx_end(mat, used);
    g_stats.tris += tris;
    g_stats.verts_out += used;
    g_stats.hull_out += used;
    ++g_stats.hull_models;
    return true;
}

// Offset (model units per int8 normal step) that puts the hull `outline_px` pixels out at depth W, for a world scale.
static float hull_offset(const Camera& cam, float W, float world_scale) {
    return g_cel.outline_px * W / (cam.focal * (world_scale > 1e-6f ? world_scale : 1.0f)) * (1.0f / 127.0f);
}


// Perspective divide for W >= near_w > 0: FSRRA (1/|W|) instead of a 10+ cycle FDIV.
static inline float inv_w(float W) { return shz_invf_fsrra(W); }

static inline uint32_t shade(uint32_t base, float lit, const float tint[4]) {
    float r = (float)((base >> 16) & 255) * lit * tint[1];
    float g = (float)((base >> 8) & 255) * lit * tint[2];
    float b = (float)(base & 255) * lit * tint[3];
    float a = (float)(base >> 24) * tint[0];
    uint32_t ri = r < 255.0f ? (uint32_t)r : 255u, gi = g < 255.0f ? (uint32_t)g : 255u;   // ordered compares
    uint32_t bi = b < 255.0f ? (uint32_t)b : 255u, ai = a < 255.0f ? (uint32_t)a : 255u;
    return (ai << 24) | (ri << 16) | (gi << 8) | bi;
}

static float max_scale(const Mat34& w) {
    float s = 0.0f;
    for (int j = 0; j < 3; ++j) {
        float l = shz_mag_sqr3f(w.m[0][j], w.m[1][j], w.m[2][j]);
        if (l > s) s = l;
    }
    return shz_sqrtf(s);
}

// Bounding sphere vs the view frustum. Returns 0 = outside, 1 = inside, -1 = crosses the near plane.
static int sphere_vis(const Camera& cam, Vec3 centre_world, float r) {
    Vec3 c = mat_point(cam.view, centre_world);
    float depth = -c.z;
    if (depth + r < cam.near_w) return 0;
    if ((shz_fabsf(c.x) - depth * cam.tan_x) * cam.cos_x > r) return 0;
    if ((shz_fabsf(c.y) - depth * cam.tan_y) * cam.cos_y > r) return 0;
    return depth - r < cam.near_w ? -1 : 1;
}

// Bounding spheres of every strip of the static models (computed once at load). Load-time and
// conservative (a culling bound), so the radius keeps the exact sqrtf rather than the FSRRA one.
const int MAX_STRIP_SPHERES = 16384;
static float g_strip_sph[MAX_STRIP_SPHERES * 4 + 16];       // + room for draw_static's lookahead
static int g_strip_sph_used = 0;

static const float* compute_strip_spheres(const dmf::Model& m) {
    int n = 0;
    for (int k = 0; k < m.h->num_batches; ++k) n += m.batches[k].num_strips;
    if (g_strip_sph_used + n > MAX_STRIP_SPHERES) return nullptr;
    float* out = g_strip_sph + 4 * g_strip_sph_used;
    g_strip_sph_used += n;
    float* o = out;
    for (int k = 0; k < m.h->num_batches; ++k) {
        const uint16_t* w = m.strips + m.batches[k].first_word;
        const uint16_t* end = w + m.batches[k].num_words;
        while (w < end) {
            const int len = *w++;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (int j = 0; j < len; ++j) {
                const dmf::Vertex& v = m.verts[w[j]];
                const float p[3] = { v.x, v.y, v.z };
                for (int a = 0; a < 3; ++a) { if (p[a] < lo[a]) lo[a] = p[a]; if (p[a] > hi[a]) hi[a] = p[a]; }
            }
            o[0] = (lo[0] + hi[0]) * 0.5f; o[1] = (lo[1] + hi[1]) * 0.5f; o[2] = (lo[2] + hi[2]) * 0.5f;
            float r2 = 0.0f;
            for (int j = 0; j < len; ++j) {
                const dmf::Vertex& v = m.verts[w[j]];
                const float d2 = shz_mag_sqr3f(v.x - o[0], v.y - o[1], v.z - o[2]);
                if (d2 > r2) r2 = d2;
            }
            o[3] = sqrtf(r2);
            o += 4;
            w += len;
        }
    }
    return out;
}

bool gfx_init(ModelGfx* g, const dmf::Model& m, bool cull) {
    memset(g, 0, sizeof *g);
    if (!m.h) return false;
    g->m = &m;
    if (!m.h->num_bones) g->strip_spheres = compute_strip_spheres(m);
    if (g_cel.outline_px > 0.0f && m.h->num_verts <= 4096) { weld_normals(m); g->hull_ok = true; }   // cel prototype
    for (int k = 0; k < m.num_mats; ++k) {
        int f = 0;
        if (m.mat_flags[k] & dmf::MAT_BILINEAR) f |= eng::MAT_BILINEAR;
        if (m.mat_flags[k] & dmf::MAT_ALPHA) f |= eng::MAT_TRANSLUCENT;
        g->doubled[k] = eng::material_create(m.mat_tex[k], f);
        g->single[k] = cull ? eng::material_create(m.mat_tex[k], f | eng::MAT_CULL) : g->doubled[k];
    }
    return true;
}

// Copies the strips of every batch from g_xv into the vertex arena (one arena batch per model batch):
// per vertex, SH4ZAM's shz_dcache_alloc_line (MOVCA.L) claims the destination cache line without
// reading RAM, then the 32 bytes are copied as words; the strip's last vertex gets the
// end-of-strip command. (Word copies rather than shz_memcpy32_1: in a loop this tight, the latter
// keeps its source pointer in memory - see tests/sh4zam/README.md.)
// cull_offscreen: strips lying wholly beyond one screen edge are not submitted (props that are
// only partly in view - a house at the top of the screen - would otherwise send all their strips
// to the TA). Costs a few compares per vertex.
static bool emit_batches(const ModelGfx& g, bool cull_offscreen = false) {
    const dmf::Model& m = *g.m;
    for (int k = 0; k < m.h->num_batches; ++k) {
        const dmf::Batch& bt = m.batches[k];
        const int count = (int)(bt.num_words - bt.num_strips);
        eng::Vert3* out = eng::vtx_begin(count);
        if (!out) return false;
        const uint16_t* w = m.strips + bt.first_word;
        const uint16_t* end = w + bt.num_words;
        eng::Vert3* o = out;
        int tris = 0;
        while (w < end) {
            const int len = *w++;
            SHZ_PREFETCH(w + 16);                           // the strip words, 32 bytes ahead
            if (cull_offscreen) {
                int l = 0, r = 0, t = 0, b = 0;
                for (int j = 0; j < len; ++j) {
                    const eng::Vert3& x = g_xv[w[j]];
                    l += x.x < 0.0f; r += x.x > 640.0f; t += x.y < 0.0f; b += x.y > 480.0f;
                }
                if (l == len || r == len || t == len || b == len) { w += len; continue; }
            }
            tris += len - 2;
            // One 32-byte cache line per vertex: MOVCA.L allocates it without a read from RAM
            // (all 8 words are written), then eight word moves.
            auto copy = [&](const eng::Vert3* src) {
                const u32a* sv32 = reinterpret_cast<const u32a*>(src);
                u32a* d = reinterpret_cast<u32a*>(o);
                shz_dcache_alloc_line(o);
                d[0] = eng::VERT; d[1] = sv32[1]; d[2] = sv32[2]; d[3] = sv32[3];
                d[4] = sv32[4]; d[5] = sv32[5]; d[6] = sv32[6]; d[7] = 0;
                ++o;
            };
            // The strip reads g_xv in index order, i.e. all over a buffer several times the size
            // of the operand cache (the Knight's is 51 KiB), so without help nearly every copy
            // would wait for a line fill. Software pipeline: `a` is the vertex being copied, `b`
            // the next; each step computes the source two strip positions ahead, requests its line
            // (PREF), copies `a` and shifts. The first vertex was requested by the previous strip's
            // lookahead, which runs on across the length word into this strip; past the last strip
            // it reads the zero padding dmf::load() leaves after every model (a harmless request).
            const eng::Vert3* a = &g_xv[w[0]];
            const eng::Vert3* b = &g_xv[w[1]];
            SHZ_PREFETCH(b);
            for (int j = 0; j < len; ++j) {
                const eng::Vert3* c = &g_xv[w[j + 2]];
                SHZ_PREFETCH(c);
                copy(a);
                a = b; b = c;
            }
            o[-1].flags = eng::VERT_EOL;
            w += len;
        }
        const int mi = bt.flags >> 8;
        const int used = (int)(o - out);
        eng::vtx_end((bt.flags & dmf::BATCH_DOUBLE_SIDED) ? g.doubled[mi] : g.single[mi], used);
        g_stats.tris += tris;
        g_stats.verts_out += used;
    }
    return true;
}

bool draw_model(const Camera& cam, const Light& light, const ModelGfx& g, const Mat34& world, const Mat34* skin, uint32_t tint,
                const uint32_t* prelit) {
    if (!g.m || !g.m->h) return false;
    // animated limbs can leave the bind-pose sphere a little: 25 % margin
    return draw_model_in(cam, light, g, world, skin, mat_point(world, g.m->centre), g.m->radius * max_scale(world) * 1.25f, tint, prelit);
}

bool draw_model_in(const Camera& cam, const Light& light, const ModelGfx& g, const Mat34& world, const Mat34* skin,
                   Vec3 bound_centre, float bound_radius, uint32_t tint, const uint32_t* prelit) {
    if (!g.m || !g.m->h) return false;
    const dmf::Model& m = *g.m;
    ++g_stats.models;
    const int nv = m.h->num_verts;
    if (nv > MAX_XV) return false;
    int vis = sphere_vis(cam, bound_centre, bound_radius);
    if (vis <= 0) { ++g_stats.models_culled; return false; }   // -1: too close to clip per-triangle; skip

    const int nb = skin ? m.h->num_bones : 0;
    // Per bone: XF = screen * world * skin (the viewport is folded into cam.screen, so the loops
    // below need two multiply-adds less per vertex). All products are XMTRX (FTRV) work.
    Mat34 xf[dmf::MAX_BONES + 1];
    Vec3  lb[dmf::MAX_BONES + 1];
    const float n_scale = 1.0f / 127.0f;
    Mat34 sw;                                              // screen * world
    xmtrx_load(cam.screen);
    xmtrx_apply(world);
    xmtrx_store(&sw);
    if (nb == 0) {
        xf[0] = sw;
    } else {
        for (int b = 0; b < nb; ++b) {
            xmtrx_load(sw);
            xmtrx_apply(skin[b]);
            xmtrx_store(&xf[b]);
        }
    }
    // Light direction in each bone's space - only needed when the colours are not baked.
    if (!prelit) {
        if (nb == 0) {
            lb[0] = normalize(mat_dir_t(world, light.dir)) * n_scale;
        } else {
            for (int b = 0; b < nb; ++b) lb[b] = normalize(mat_dir_t(mat_mul(world, skin[b]), light.dir)) * n_scale;
        }
    }
    const float tn[4] = { (float)(tint >> 24) / 255.0f, (float)((tint >> 16) & 255) / 255.0f,
                          (float)((tint >> 8) & 255) / 255.0f, (float)(tint & 255) / 255.0f };
    // One base colour for the whole model (true for the KayKit Knight and the placeholders):
    // shade through a 256-entry table instead of 4 multiplies + 4 float->int casts per vertex.
    static uint32_t lut[256];
    const bool use_lut = !prelit && m.uniform && nv > 256;
    if (use_lut)
        for (int i = 0; i < 256; ++i) lut[i] = shade(m.uniform_argb, (float)i * (1.0f / 255.0f), tn);
    const float amb = light.ambient, dif = light.diffuse;
    const float near_w = cam.near_w;
    const bool toon = g_cel.toon >= 0.5f;

    // Finishes one vertex: perspective divide (screen mapping is in the matrix), lighting.
    auto finish = [&](eng::Vert3* o, const dmf::Vertex* v, float X, float Y, float W, float ndl) {
        if (W < near_w) W = near_w;                  // cannot happen after the sphere test; belt and braces
        const float inv = inv_w(W);
        if (toon) ndl = cel_ndl(ndl);                // cel prototype: banded lighting
        float lit = amb + (ndl > 0.0f ? dif * ndl : 0.0f);
        // Ordered compare only (NaN -> 1.0): on the SH4 a negated `<=` needs an unordered test,
        // which GCC's default -mieee code does with a soft-float __unordsf2 call.
        lit = lit < 1.0f ? lit : 1.0f;
        put_vert(o, X * inv, Y * inv, inv, fbits(v->u), fbits(v->v), use_lut ? lut[(int)(lit * 255.0f)] : shade(v->argb, lit, tn));
    };
    // XMTRX holds one bone matrix at a time, so two-bone groups run two passes (bone 0, then
    // bone 1 + blend by the vertex weight).
    for (int gi = 0; gi < m.h->num_groups; ++gi) {
        const dmf::Group& gr = m.groups[gi];
        const int b0 = nb ? gr.bone0 : 0;
        const bool two = nb && gr.bone1 != dmf::NO_BONE;
        const int count = gr.count;                      // local: the stores below may alias the model data
        const dmf::Vertex* v = m.verts + gr.first;
        eng::Vert3* o = g_xv + gr.first;
        xmtrx_load(xf[b0]);
        if (prelit && !two) {
            const uint32_t* pc = prelit + gr.first;
            for (int i = 0; i < count; ++i, ++v, ++o) {
                SHZ_PREFETCH(v + 2);                                    // vertices are 32-byte lines, read in order
                SHZ_PREFETCH(pc + i + 16);                              // baked colours: 8 per line, two lines ahead
                const Vec3 s = xmtrx_point(v->x, v->y, v->z);          // (X', Y', W): one FTRV
                const float inv = inv_w(s.z < near_w ? near_w : s.z);
                put_vert(o, s.x * inv, s.y * inv, inv, fbits(v->u), fbits(v->v), pc[i]);
            }
            continue;
        }
        if (!two) {
            const Vec3 la = lb[b0];
            for (int i = 0; i < count; ++i, ++v, ++o) {
                SHZ_PREFETCH(v + 2);
                const Vec3 s = xmtrx_point(v->x, v->y, v->z);
                finish(o, v, s.x, s.y, s.z, shz_dot6f((float)v->nx, (float)v->ny, (float)v->nz, la.x, la.y, la.z));
            }
            continue;
        }
        XT* t = g_xt + gr.first;
        if (prelit) {                                            // two bones, baked colours: no lighting
            for (int i = 0; i < count; ++i, ++v, ++t) {      // pass 1: bone0
                SHZ_PREFETCH(v + 2);
                const Vec3 s = xmtrx_point(v->x, v->y, v->z);
                xt_claim(t);
                t->X = s.x; t->Y = s.y; t->W = s.z;
            }
            xmtrx_load(xf[gr.bone1]);
            v = m.verts + gr.first;
            t = g_xt + gr.first;
            const uint32_t* pc = prelit + gr.first;
            for (int i = 0; i < count; ++i, ++v, ++t, ++o) { // pass 2: bone1, blend by w0
                SHZ_PREFETCH(v + 2);                         // the group's vertices again, and pass 1's
                SHZ_PREFETCH(t + 4);                         // results, two lines ahead
                SHZ_PREFETCH(pc + i + 16);
                const Vec3 s = xmtrx_point(v->x, v->y, v->z);
                const float w0 = (float)v->w0 * (1.0f / 255.0f);
                const float X = s.x + (t->X - s.x) * w0, Y = s.y + (t->Y - s.y) * w0;
                float W = s.z + (t->W - s.z) * w0;
                if (W < near_w) W = near_w;
                const float inv = inv_w(W);
                put_vert(o, X * inv, Y * inv, inv, fbits(v->u), fbits(v->v), pc[i]);
            }
            continue;
        }
        const Vec3 la = lb[b0];
        for (int i = 0; i < count; ++i, ++v, ++t) {          // pass 1: bone0
            SHZ_PREFETCH(v + 2);
            const Vec3 s = xmtrx_point(v->x, v->y, v->z);
            xt_claim(t);
            t->X = s.x; t->Y = s.y; t->W = s.z;
            t->ndl = shz_dot6f((float)v->nx, (float)v->ny, (float)v->nz, la.x, la.y, la.z);
        }
        xmtrx_load(xf[gr.bone1]);
        const Vec3 lbb = lb[gr.bone1];
        v = m.verts + gr.first;
        t = g_xt + gr.first;
        for (int i = 0; i < count; ++i, ++v, ++t, ++o) {    // pass 2: bone1, blend by w0
            SHZ_PREFETCH(v + 2);
            SHZ_PREFETCH(t + 4);
            const Vec3 s = xmtrx_point(v->x, v->y, v->z);
            const float w0 = (float)v->w0 * (1.0f / 255.0f);
            const float n1 = shz_dot6f((float)v->nx, (float)v->ny, (float)v->nz, lbb.x, lbb.y, lbb.z);
            finish(o, v, s.x + (t->X - s.x) * w0, s.y + (t->Y - s.y) * w0, s.z + (t->W - s.z) * w0, n1 + (t->ndl - n1) * w0);
        }
    }
    g_stats.verts_xf += nv;

    if (!emit_batches(g)) return false;
    // cel prototype: the outline hull - the same skinning with every vertex pushed out along its welded normal
    const bool lod = g_cel.lod >= 0.5f && g.hull && g.hull->m && g.hull->hull_ok;
    if (g_hull_chars && (g.hull_ok || lod) && g_cel.outline_px > 0.0f) {
        const dmf::Model& hm = lod ? *g.hull->m : m;        // the decimated hull mesh, or the model itself
        const float W = -mat_point(cam.view, bound_centre).z;
        const float k = hull_offset(cam, W, max_scale(world));
        const float zp = g_cel.zpush;
        const uint32_t col = hull_colour();
        for (int gi = 0; gi < hm.h->num_groups; ++gi) {
            const dmf::Group& gr = hm.groups[gi];
            const int b0 = nb ? gr.bone0 : 0;
            const bool two = nb && gr.bone1 != dmf::NO_BONE;
            const int count = gr.count;
            const dmf::Vertex* v = hm.verts + gr.first;
            eng::Vert3* o = g_xv + gr.first;
            xmtrx_load(xf[b0]);
            if (!two) {
                for (int i = 0; i < count; ++i, ++v, ++o) {
                    SHZ_PREFETCH(v + 2);
                    const Vec3 s = xmtrx_point(v->x + (float)(int8_t)v->pad[0] * k, v->y + (float)(int8_t)v->pad[1] * k,
                                               v->z + (float)(int8_t)v->pad[2] * k);
                    const float Wv = s.z < near_w ? near_w : s.z;
                    const float inv = inv_w(Wv);
                    put_vert(o, s.x * inv, s.y * inv, zp > 0.0f ? inv_w(Wv + zp) : inv, 0u, 0u, col);
                }
                continue;
            }
            XT* t = g_xt + gr.first;
            for (int i = 0; i < count; ++i, ++v, ++t) {
                SHZ_PREFETCH(v + 2);
                const Vec3 s = xmtrx_point(v->x + (float)(int8_t)v->pad[0] * k, v->y + (float)(int8_t)v->pad[1] * k,
                                           v->z + (float)(int8_t)v->pad[2] * k);
                xt_claim(t);
                t->X = s.x; t->Y = s.y; t->W = s.z;
            }
            xmtrx_load(xf[gr.bone1]);
            v = hm.verts + gr.first;
            t = g_xt + gr.first;
            for (int i = 0; i < count; ++i, ++v, ++t, ++o) {
                SHZ_PREFETCH(v + 2);
                SHZ_PREFETCH(t + 4);
                const Vec3 s = xmtrx_point(v->x + (float)(int8_t)v->pad[0] * k, v->y + (float)(int8_t)v->pad[1] * k,
                                           v->z + (float)(int8_t)v->pad[2] * k);
                const float w0 = (float)v->w0 * (1.0f / 255.0f);
                float W2 = s.z + (t->W - s.z) * w0;
                if (W2 < near_w) W2 = near_w;
                const float inv = inv_w(W2);
                put_vert(o, (s.x + (t->X - s.x) * w0) * inv, (s.y + (t->Y - s.y) * w0) * inv, zp > 0.0f ? inv_w(W2 + zp) : inv, 0u, 0u, col);
            }
        }
        g_stats.verts_xf += hm.h->num_verts;
        g_stats.hull_xf += hm.h->num_verts;
        emit_hull_strips(hm);
    }
    return true;
}

// ---------------------------------------------------------------------------- polygons
struct CV { float X, Y, W, u, v; float c[4]; };

static CV cv_lerp(const CV& a, const CV& b, float t) {
    CV r;
    r.X = shz_lerpf(a.X, b.X, t); r.Y = shz_lerpf(a.Y, b.Y, t); r.W = shz_lerpf(a.W, b.W, t);
    r.u = shz_lerpf(a.u, b.u, t); r.v = shz_lerpf(a.v, b.v, t);
    for (int i = 0; i < 4; ++i) r.c[i] = shz_lerpf(a.c[i], b.c[i], t);
    return r;
}

// Clobbers XMTRX (the corners are transformed as one batch).
int emit_poly(const Camera& cam, const PolyVert* in, int n, eng::Vert3* out) {
    if (n < 3 || n > 8) return 0;
    CV a[8], b[16];
    int na = 0, inside = 0;
    xmtrx_load(cam.proj_view);
    for (int i = 0; i < n; ++i) {
        const PolyVert& p = in[i];
        const Vec3 s = xmtrx_point(p.p);
        CV& r = a[na];
        r.X = s.x; r.Y = s.y; r.W = s.z; r.u = p.u; r.v = p.v;
        r.c[0] = (float)(int)(p.argb >> 24); r.c[1] = (float)(int)((p.argb >> 16) & 255);
        r.c[2] = (float)(int)((p.argb >> 8) & 255); r.c[3] = (float)(int)(p.argb & 255);
        inside += r.W >= cam.near_w;
        ++na;
    }
    const CV* poly = a;
    int np = na;
    if (inside == 0) return 0;
    if (inside < n) {                                   // Sutherland-Hodgman against W = near
        np = 0;
        for (int i = 0; i < na; ++i) {
            const CV& p = a[i];
            const CV& q = a[(i + 1) % na];
            const float dp = p.W - cam.near_w, dq = q.W - cam.near_w;
            if (dp >= 0) b[np++] = p;
            if ((dp >= 0) != (dq >= 0)) b[np++] = cv_lerp(p, q, dp / (dp - dq));
        }
        poly = b;
        if (np < 3) return 0;
    }
    // strip order for a convex loop: v0, v1, v(n-1), v2, v(n-2), ...  (keeps the loop's winding)
    int lo = 1, hi = np - 1;
    for (int k = 0; k < np; ++k) {
        int idx = k == 0 ? 0 : (k % 2 == 1 ? lo++ : hi--);
        const CV& c = poly[idx];
        const float inv = inv_w(c.W);                   // clipped: W >= near_w > 0
        eng::Vert3& o = out[k];
        shz_dcache_alloc_line(&o);                      // whole 32-byte line written below
        o.flags = k == np - 1 ? eng::VERT_EOL : eng::VERT;
        o.x = cam.cx + c.X * inv; o.y = cam.cy + c.Y * inv; o.z = inv;
        o.u = c.u; o.v = c.v;
        o.argb = ((uint32_t)(int)(c.c[0] + 0.5f) << 24) | ((uint32_t)(int)(c.c[1] + 0.5f) << 16) |
                 ((uint32_t)(int)(c.c[2] + 0.5f) << 8) | (uint32_t)(int)(c.c[3] + 0.5f);
        o.oargb = 0;
    }
    g_stats.verts_out += np;
    g_stats.tris += np - 2;
    return np;
}

// ---------------------------------------------------------------------------- static props
void prelight(const dmf::Model& m, const Mat34& world, const Light& light, uint32_t* colors) {
    if (m.h->flags & dmf::DMF_PRELIT) {            // baked offline (tools/vs1art): the vertex colour is final
        for (int i = 0; i < m.h->num_verts; ++i) colors[i] = m.verts[i].argb;
        return;
    }
    const Vec3 l = normalize(mat_dir_t(world, light.dir)) * (1.0f / 127.0f);     // light in model space
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (int i = 0; i < m.h->num_verts; ++i) {
        const dmf::Vertex& v = m.verts[i];
        const float ndl = cel_ndl(shz_dot6f((float)v.nx, (float)v.ny, (float)v.nz, l.x, l.y, l.z));
        float lit = light.ambient + (ndl > 0.0f ? light.diffuse * ndl : 0.0f);
        lit = lit < 1.0f ? lit : 1.0f;
        colors[i] = shade(v.argb, lit, white);
    }
}

// Model-space bounding box vs the view frustum: 0 = outside (all 8 corners beyond one plane),
// 1 = in front of the near plane (possibly crossing a screen edge), 2 = wholly on screen,
// -1 = crosses the near plane. Much tighter than the sphere for tall, wide
// props (a house's sphere still touched the view when the house was well above the screen).
// edges: which screen edges the box crosses (1 left, 2 right, 4 top, 8 bottom), for vis == 1.
// Clobbers XMTRX (view * world, then the 8 corners as one FTRV batch).
static int box_vis(const Camera& cam, const dmf::Model& m, const Mat34& world, int* edges = nullptr) {
    xmtrx_load(cam.view);
    xmtrx_apply(world);
    int out_l = 0, out_r = 0, out_t = 0, out_b = 0, behind = 0;
    for (int k = 0; k < 8; ++k) {
        const Vec3 c = xmtrx_point((k & 1) ? m.h->bmax[0] : m.h->bmin[0], (k & 2) ? m.h->bmax[1] : m.h->bmin[1],
                                   (k & 4) ? m.h->bmax[2] : m.h->bmin[2]);
        const float depth = -c.z;
        behind += depth < cam.near_w;
        out_l += c.x < -depth * cam.tan_x;
        out_r += c.x > depth * cam.tan_x;
        out_b += c.y < -depth * cam.tan_y;
        out_t += c.y > depth * cam.tan_y;
    }
    if (behind == 8 || out_l == 8 || out_r == 8 || out_t == 8 || out_b == 8) return 0;
    if (behind) return -1;
    if (edges) *edges = (out_l ? 1 : 0) | (out_r ? 2 : 0) | (out_t ? 4 : 0) | (out_b ? 8 : 0);
    return (out_l | out_r | out_t | out_b) ? 1 : 2;          // 2: wholly on screen
}

void prelight_posed(const dmf::Model& m, const Mat34* skin, float yaw, const Light& light, uint32_t* colors) {
    prelight_posed_range(m, skin, yaw, light, colors, 0, m.h->num_verts);
}

void prelight_posed_range(const dmf::Model& m, const Mat34* skin, float yaw, const Light& light, uint32_t* colors,
                          int v_begin, int v_end) {
    const Mat34 rot = mat_trs_y(v3(0, 0, 0), yaw, 1, 1, 1);
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (int gi = 0; gi < m.h->num_groups; ++gi) {
        const dmf::Group& gr = m.groups[gi];
        const int g0 = gr.first > v_begin ? gr.first : v_begin, g1 = gr.first + gr.count < v_end ? gr.first + gr.count : v_end;
        if (g0 >= g1) continue;                                      // group outside the range
        const bool two = gr.bone1 != dmf::NO_BONE;
        const Mat34 a = skin ? mat_mul(rot, skin[gr.bone0]) : rot;
        const Mat34 b = (skin && two) ? mat_mul(rot, skin[gr.bone1]) : a;
        for (int i = g0; i < g1; ++i) {
            const dmf::Vertex& v = m.verts[i];
            const Vec3 n = v3((float)v.nx, (float)v.ny, (float)v.nz);
            const float w0 = two ? (float)v.w0 * (1.0f / 255.0f) : 1.0f;
            const Vec3 nw = normalize(lerp(mat_dir(b, n), mat_dir(a, n), w0));
            const float ndl = cel_ndl(dot(nw, light.dir));
            float lit = light.ambient + (ndl > 0.0f ? light.diffuse * ndl : 0.0f);
            lit = lit < 1.0f ? lit : 1.0f;
            colors[i] = shade(v.argb, lit, white);
        }
    }
}

bool draw_static(const Camera& cam, const ModelGfx& g, const Mat34& world, const uint32_t* colors) {
    if (!g.m || !g.m->h) return false;
    const dmf::Model& m = *g.m;
    ++g_stats.statics;
    const int nv = m.h->num_verts;
    if (nv > MAX_XV) return false;
    int edges = 0;
    const int vis = box_vis(cam, m, world, &edges);
    if (vis == 0) { ++g_stats.statics_culled; return false; }
    // XMTRX = S = screen * world, kept there for every transform below (strip spheres and
    // vertices); nothing in between reloads it (vtx_begin/vtx_end do not touch the FPU).
    xmtrx_load(cam.screen);
    xmtrx_apply(world);
    const float near_w = cam.near_w;
    if (vis > 0) {
        // Wholly in front of the near plane (the usual case): transform each strip vertex straight
        // into the vertex arena - props share few vertices between strips (~1.2 emissions per
        // vertex), so this beats transforming into a scratch buffer and copying. Each strip's
        // bounding sphere is tested first (~20 instructions per strip): strips off screen cost
        // neither transforms nor TA bandwidth (half-visible houses at the screen edges).
        // XMTRX = S for the whole instance: one FTRV + one FSRRA per emitted vertex.
        const float* sph = (vis == 2 || !g_strip_culling) ? nullptr : g.strip_spheres;   // wholly on screen: no tests
        const dmf::Vertex* const verts = m.verts;        // locals: the stores below cannot alias them
        const uint32_t* const cols = colors;
        const float ws = max_scale(world);
        const float kx = cam.focal * cam.sec_x * ws;
        const float ky = cam.focal * cam.sec_y * ws;
        // Strip test: a strip is culled when its bounding sphere lies wholly beyond one screen edge.
        // With the sphere centre transformed by S (one FTRV: XMTRX already holds S) to (X', Y', W),
        // each edge is one compare against radius * k (k scales the radius into these units):
        //   left  x < 0   : -X'        > r kx        right  x > 640 : X' - 640 W > r kx
        //   top   y < 0   : -Y'        > r ky        bottom y > 480 : Y' - 480 W > r ky
        // Only the edges the model's box crosses are tested (a strip cannot be wholly beyond an
        // edge that every box corner is inside of) - usually one.
        const float sw = 2.0f * cam.cx, sh = 2.0f * cam.cy;
        for (int k = 0; k < m.h->num_batches; ++k) {
            const dmf::Batch& bt = m.batches[k];
            eng::Vert3* out = eng::vtx_begin((int)(bt.num_words - bt.num_strips));
            if (!out) return false;
            eng::Vert3* o = out;
            const uint16_t* w = m.strips + bt.first_word;
            const uint16_t* end = w + bt.num_words;
            int tris = 0;
            while (w < end) {
                const int len = *w++;
                SHZ_PREFETCH(w + 16);                        // the strip words, 32 bytes ahead
                if (sph) {
                    SHZ_PREFETCH(sph + 16);                  // strip spheres: 16 bytes each, two lines ahead (the array has room)
                    const Vec3 c = xmtrx_point(sph[0], sph[1], sph[2]);          // (X', Y', W) of the centre
                    const float rx = sph[3] * kx, ry = sph[3] * ky;
                    sph += 4;
                    const bool off = ((edges & 1) && -c.x > rx) || ((edges & 2) && c.x - sw * c.z > rx) ||
                                     ((edges & 4) && -c.y > ry) || ((edges & 8) && c.y - sh * c.z > ry);
                    if (off) {
                        w += len;
                        ++g_stats.strips_culled;
                        continue;
                    }
                }
                g_stats.verts_xf += len;
                tris += len - 2;
                auto emit = [&](const dmf::Vertex* v, uint32_t col) {
                    const Vec3 s = xmtrx_point(v->x, v->y, v->z);     // (X', Y', W): one FTRV
                    const float inv = inv_w(s.z);
                    put_vert(o, s.x * inv, s.y * inv, inv, fbits(v->u), fbits(v->v), col);
                    ++o;
                };
                // The strip indexes the model's vertices (32-byte lines, e.g. 64 KiB for a house):
                // the same software pipeline as emit_batches - the vertex two strip positions ahead
                // is requested (PREF) before the current one is transformed.
                const dmf::Vertex* a = verts + w[0];
                const dmf::Vertex* b = verts + w[1];
                SHZ_PREFETCH(b);
                for (int j = 0; j < len; ++j) {
                    const dmf::Vertex* c = verts + w[j + 2];
                    SHZ_PREFETCH(c);
                    emit(a, cols[w[j]]);
                    a = b; b = c;
                }
                w += len;
                o[-1].flags = eng::VERT_EOL;
            }

            const int mi = bt.flags >> 8;
            const int used = (int)(o - out);
            eng::vtx_end((bt.flags & dmf::BATCH_DOUBLE_SIDED) ? g.doubled[mi] : g.single[mi], used);
            g_stats.tris += tris;
            g_stats.verts_out += used;
        }
        // cel prototype: the outline hull of a static prop (small props only unless cel().props >= 2)
        if (g_hull_props && g.hull_ok && g_cel.outline_px > 0.0f && (g_cel.props >= 1.5f || nv < CEL_PROP_MAX_VERTS)) {
            const float W = -mat_point(cam.view, mat_point(world, m.centre)).z;
            const float k = hull_offset(cam, W, ws);
            const uint32_t col = hull_colour();
            xmtrx_load(cam.screen);
            xmtrx_apply(world);
            const dmf::Vertex* v = m.verts;
            eng::Vert3* ho = g_xv;
            for (int i = 0; i < nv; ++i, ++v, ++ho) {
                SHZ_PREFETCH(v + 2);
                const Vec3 s = xmtrx_point(v->x + (float)(int8_t)v->pad[0] * k, v->y + (float)(int8_t)v->pad[1] * k,
                                           v->z + (float)(int8_t)v->pad[2] * k);
                const float inv = inv_w(s.z < near_w ? near_w : s.z);
                put_vert(ho, s.x * inv, s.y * inv, inv, 0u, 0u, col);
            }
            g_stats.verts_xf += nv;
            g_stats.hull_xf += nv;
            emit_hull_strips(m);
        }
        return true;
    }
    // Crosses the near plane (rare: a prop right under the camera). Every vertex is transformed
    // once (W kept for the near test); triangles wholly in front go out as they are, the others
    // are clipped (emit_poly, which reloads XMTRX - the transforms are done by then). Nothing pops
    // at the screen edge.
    const dmf::Vertex* v = m.verts;
    eng::Vert3* o = g_xv;
    XT* t = g_xt;
    for (int i = 0; i < nv; ++i, ++v, ++o, ++t) {
        SHZ_PREFETCH(v + 2);
        const Vec3 s = xmtrx_point(v->x, v->y, v->z);
        t->W = s.z;
        const float inv = inv_w(s.z < near_w ? near_w : s.z);
        put_vert(o, s.x * inv, s.y * inv, inv, fbits(v->u), fbits(v->v), colors[i]);
    }
    g_stats.verts_xf += nv;
    ++g_stats.statics_near;
    // Triangles wholly in front of the near plane go out as runs of the model's own strips (a run starting on
    // an odd triangle begins with a repeated vertex, which keeps the strip parity, i.e. the winding); those
    // wholly beyond one screen edge are dropped (a house under the camera is mostly below the screen); the
    // rest - crossing the near plane - are clipped one by one. (VS1 art slice: the former path sent every
    // triangle as its own 3-vertex strip - 6,900 vertices for a VS1 house under the camera. set_near_strips(false)
    // brings it back for the A/B test: tests/vs1art/run.sh checks the pixels are identical.)
    if (!g_near_strips) {
        for (int k = 0; k < m.h->num_batches; ++k) {
            const dmf::Batch& bt = m.batches[k];
            eng::Vert3* out = eng::vtx_begin((int)bt.num_tris * 4);
            if (!out) return false;
            int used = 0;
            const uint16_t* w = m.strips + bt.first_word;
            const uint16_t* end = w + bt.num_words;
            while (w < end) {
                const int len = *w++;
                for (int j = 0; j + 2 < len; ++j) {
                    int a = w[j], b = w[j + 1], c = w[j + 2];
                    if (j & 1) { int tmp = a; a = b; b = tmp; }
                    if (g_xt[a].W >= near_w && g_xt[b].W >= near_w && g_xt[c].W >= near_w) {
                        const int idx[3] = { a, b, c };
                        for (int q = 0; q < 3; ++q) shz_memcpy32_1(&out[used + q], &g_xv[idx[q]]);
                        out[used + 2].flags = eng::VERT_EOL;
                        used += 3;
                    } else if (g_xt[a].W >= near_w || g_xt[b].W >= near_w || g_xt[c].W >= near_w) {
                        const int idx[3] = { a, b, c };
                        PolyVert pv[3];
                        for (int q = 0; q < 3; ++q) {
                            const dmf::Vertex& mv = m.verts[idx[q]];
                            pv[q].p = mat_point(world, v3(mv.x, mv.y, mv.z));
                            pv[q].u = mv.u; pv[q].v = mv.v; pv[q].argb = colors[idx[q]];
                        }
                        used += emit_poly(cam, pv, 3, out + used);
                    }
                }
                w += len;
            }
            const int mi = bt.flags >> 8;
            eng::vtx_end((bt.flags & dmf::BATCH_DOUBLE_SIDED) ? g.doubled[mi] : g.single[mi], used);
            g_stats.verts_out += used;
        }
        return true;
    }
    const float sw = 2.0f * cam.cx, sh = 2.0f * cam.cy;
    for (int k = 0; k < m.h->num_batches; ++k) {
        const dmf::Batch& bt = m.batches[k];
        // bound: 5 per clipped triangle - far more than is ever written (a house under the camera writes a few hundred
        // of its ~15,000). Slice 3D-6: reserve at most what the frame's arena has left and stop the batch if it would run
        // over, instead of refusing the whole batch (a drop) when the bound does not fit
        const int bound = (int)bt.num_tris * 5 + 2 * (int)bt.num_strips, room = eng::vtx_room();
        const int cap = bound < room ? bound : room;
        if (bound > room && room < 16) { eng::vtx_begin(bound); return false; }   // refused (a drop): the arena is really full
        eng::Vert3* out = eng::vtx_begin(cap);
        if (!out) return false;
        int used = 0, tris = 0;
        bool full = false;
        const uint16_t* w = m.strips + bt.first_word;
        const uint16_t* end = w + bt.num_words;
        while (w < end && !full) {
            const int len = *w++;
            bool open = false;
            for (int j = 0; j + 2 < len; ++j) {
                if (used + 8 > cap) { full = true; break; }          // at most 4 (a strip opening) or 5 (a clipped triangle)
                int a = w[j], b = w[j + 1], c = w[j + 2];
                const bool fa = g_xt[a].W >= near_w, fb = g_xt[b].W >= near_w, fc = g_xt[c].W >= near_w;
                if (fa && fb && fc) {
                    const eng::Vert3 &A = g_xv[a], &B = g_xv[b], &C = g_xv[c];
                    const bool off = (A.x < 0.0f && B.x < 0.0f && C.x < 0.0f) || (A.x > sw && B.x > sw && C.x > sw) ||
                                     (A.y < 0.0f && B.y < 0.0f && C.y < 0.0f) || (A.y > sh && B.y > sh && C.y > sh);
                    if (!off) {
                        if (!open) {
                            if (j & 1) shz_memcpy32_1(&out[used++], &g_xv[a]);     // parity: (a, a, b) is degenerate
                            shz_memcpy32_1(&out[used++], &g_xv[a]);
                            shz_memcpy32_1(&out[used++], &g_xv[b]);
                            open = true;
                        }
                        shz_memcpy32_1(&out[used], &g_xv[c]);
                        out[used++].flags = eng::VERT;
                        ++tris;
                        continue;
                    }
                }
                if (open) { out[used - 1].flags = eng::VERT_EOL; open = false; }
                if (fa || fb || fc) {
                    if (!(fa && fb && fc)) {
                        if (j & 1) { int tmp = a; a = b; b = tmp; }     // strip parity: keep the winding
                        const int idx[3] = { a, b, c };
                        PolyVert pv[3];
                        for (int q = 0; q < 3; ++q) {
                            const dmf::Vertex& mv = m.verts[idx[q]];
                            pv[q].p = mat_point(world, v3(mv.x, mv.y, mv.z));
                            pv[q].u = mv.u; pv[q].v = mv.v; pv[q].argb = colors[idx[q]];
                        }
                        used += emit_poly(cam, pv, 3, out + used);
                        ++tris;
                    }
                }
            }
            if (open) out[used - 1].flags = eng::VERT_EOL;
            w += len;
        }
        const int mi = bt.flags >> 8;
        eng::vtx_end((bt.flags & dmf::BATCH_DOUBLE_SIDED) ? g.doubled[mi] : g.single[mi], used);
        g_stats.tris += tris;
        g_stats.verts_out += used;
    }
    return true;
}

void draw_blob_shadow(const Camera& cam, eng::Material mat, Vec3 c, float radius, int alpha) {
    const int SEG = 12;
    // unit-circle table (computed once with FSCA): no trig per shadow
    static float cs[SEG + 1], sn[SEG + 1];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i <= SEG; ++i) {
            const shz_sincos_t r = shz_sincosf(SHZ_F_PI * 2.0f * (float)i / SEG);
            cs[i] = r.cos; sn[i] = r.sin;
        }
        ready = true;
    }
    eng::Vert3* out = eng::vtx_begin(SEG * 5);
    if (!out) return;
    const uint32_t centre_col = (uint32_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha) << 24;
    const float y = 0.02f;                          // just above the ground plane
    // transform the centre + ring once (one XMTRX load, 13 FTRVs); a shadow close enough to cross
    // the near plane falls back to the clipper triangle by triangle
    struct P { float x, y, z; bool front; } p[SEG + 1];
    xmtrx_load(cam.screen);
    Vec3 s = xmtrx_point(c.x, y, c.z);
    bool all_front = s.z >= cam.near_w;
    if (all_front) { const float inv = inv_w(s.z); p[SEG] = P{ s.x * inv, s.y * inv, inv, true }; }
    for (int i = 0; i < SEG; ++i) {
        s = xmtrx_point(c.x + radius * cs[i], y, c.z + radius * sn[i]);
        const bool f = s.z >= cam.near_w;
        all_front = all_front && f;
        if (f) { const float inv = inv_w(s.z); p[i] = P{ s.x * inv, s.y * inv, inv, true }; }
    }
    int used = 0;
    for (int i = 0; i < SEG; ++i) {
        const int a = (i + 1) % SEG, b = i;        // centre, ring(i+1), ring(i): counter-clockwise from above
        if (all_front) {
            const P* t[3] = { &p[SEG], &p[a], &p[b] };
            for (int k = 0; k < 3; ++k) {
                eng::Vert3& o = out[used + k];
                shz_dcache_alloc_line(&o);          // whole 32-byte line written below
                o.flags = k == 2 ? eng::VERT_EOL : eng::VERT;
                o.x = t[k]->x; o.y = t[k]->y; o.z = t[k]->z; o.u = 0.0f; o.v = 0.0f;
                o.argb = k == 0 ? centre_col : 0u; o.oargb = 0;
            }
            used += 3;
            g_stats.verts_out += 3; g_stats.tris += 1;
        } else {
            PolyVert tri[3] = { { v3(c.x, y, c.z), 0, 0, centre_col },
                                { v3(c.x + radius * cs[a], y, c.z + radius * sn[a]), 0, 0, 0u },
                                { v3(c.x + radius * cs[b], y, c.z + radius * sn[b]), 0, 0, 0u } };
            used += emit_poly(cam, tri, 3, out + used);
        }
    }
    eng::vtx_end(mat, used);
}

} // namespace r3d

namespace r3d {

// ---------------------------------------------------------------------------- slice 3D-4 helpers
bool project(const Camera& cam, Vec3 p, float* sx, float* sy, float* z) {
    const Vec3 s = mat_point(cam.screen, p);               // (X', Y', W)
    if (s.z < cam.near_w) return false;
    const float inv = inv_w(s.z);
    *sx = s.x * inv; *sy = s.y * inv;
    if (z) *z = inv;
    return true;
}

// Screen-aligned quads centred on world points: one FTRV + one FSRRA per billboard, 4 vertices
// (one strip) each; billboards whose centre is behind the near plane are skipped. Sizes are world
// units (scaled by focal / W), rot rotates the quad on screen.
int draw_billboards(const Camera& cam, eng::Material mat, const Billboard* b, int n) {
    if (n <= 0) return 0;
    eng::Vert3* out = eng::vtx_begin(n * 4);
    if (!out) return 0;
    xmtrx_load(cam.screen);
    int used = 0, drawn = 0;
    for (int i = 0; i < n; ++i) {
        const Billboard& q = b[i];
        const Vec3 s = xmtrx_point(q.p);
        if (s.z < cam.near_w) continue;
        const float inv = inv_w(s.z);
        const float cx = s.x * inv, cy = s.y * inv;
        const float hw = 0.5f * q.w * cam.focal * inv, hh = 0.5f * q.h * cam.focal * inv;
        if (cx + hw < 0.0f || cx - hw > 640.0f || cy + hh < 0.0f || cy - hh > 480.0f) continue;
        float c = 1.0f, sn = 0.0f;
        if (q.rot != 0.0f) { const shz_sincos_t r = shz_sincosf(q.rot); c = r.cos; sn = r.sin; }
        const float z = inv * (1.0f + q.z_bias);
        // strip order: top-left, bottom-left, top-right, bottom-right (counter-clockwise on screen)
        const float lx[4] = { -hw, -hw, hw, hw }, ly[4] = { -hh, hh, -hh, hh };
        const float u[4] = { q.u0, q.u0, q.u1, q.u1 }, v[4] = { q.v0, q.v1, q.v0, q.v1 };
        for (int k = 0; k < 4; ++k) {
            eng::Vert3& o = out[used + k];
            shz_dcache_alloc_line(&o);                      // whole 32-byte line written below
            o.flags = k == 3 ? eng::VERT_EOL : eng::VERT;
            o.x = cx + lx[k] * c - ly[k] * sn; o.y = cy + lx[k] * sn + ly[k] * c; o.z = z;
            o.u = u[k]; o.v = v[k]; o.argb = q.argb; o.oargb = 0;
        }
        used += 4;
        ++drawn;
    }
    eng::vtx_end(mat, used);
    g_stats.verts_out += used;
    g_stats.tris += 2 * drawn;
    return drawn;
}

// Flat quads on a horizontal plane (decals: selection rings, scorch marks), clipped against the
// near plane (emit_poly). Each quad: centre, half size, rotation about +Y, uv rect, colour.
int draw_ground_quads(const Camera& cam, eng::Material mat, const GroundQuad* q, int n) {
    if (n <= 0) return 0;
    eng::Vert3* out = eng::vtx_begin(n * 8);
    if (!out) return 0;
    int used = 0;
    for (int i = 0; i < n; ++i) {
        const GroundQuad& g = q[i];
        const shz_sincos_t r = shz_sincosf(g.rot);
        const float ax = g.half * r.cos, az = g.half * r.sin;      // rotated half-axes
        PolyVert pv[4] = {
            { v3(g.c.x - ax - az, g.c.y, g.c.z - az + ax), g.u0, g.v1, g.argb },
            { v3(g.c.x + ax - az, g.c.y, g.c.z + az + ax), g.u1, g.v1, g.argb },
            { v3(g.c.x + ax + az, g.c.y, g.c.z + az - ax), g.u1, g.v0, g.argb },
            { v3(g.c.x - ax + az, g.c.y, g.c.z - az - ax), g.u0, g.v0, g.argb },
        };
        used += emit_poly(cam, pv, 4, out + used);
    }
    eng::vtx_end(mat, used);
    return used;
}

} // namespace r3d
