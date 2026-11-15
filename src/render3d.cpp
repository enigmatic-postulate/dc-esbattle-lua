// render3d.cpp - see render3d.h
//
// Per-model cost on the SH4 (what this loop is shaped for):
//   * one 3x4 "projection * view * world * skin" matrix per bone, built once per frame;
//   * vertices come pre-sorted into skin groups, so each group loads its one or two matrices
//     once and then streams its vertices (no per-vertex matrix lookups);
//   * lighting: the light direction is moved into each bone's space once per frame, so a
//     vertex's diffuse term is one or two 3-term dot products with its stored int8 normal;
//   * each distinct vertex is transformed once into a scratch array, then the strips (already
//     built by the converter) are copied into the engine's vertex arena.
#include "render3d.h"
#include <stddef.h>
#include <string.h>

namespace r3d {

static_assert(offsetof(eng::Vert3, y) == offsetof(eng::Vert3, x) + 4 && offsetof(eng::Vert3, z) == offsetof(eng::Vert3, x) + 8 &&
              offsetof(eng::Vert3, u) == offsetof(eng::Vert3, x) + 12 && offsetof(eng::Vert3, v) == offsetof(eng::Vert3, x) + 16 &&
              offsetof(eng::Vert3, argb) == offsetof(eng::Vert3, x) + 20, "XV copy relies on the Vert3 field order");

static Stats g_stats;
static bool g_strip_culling = true;
void set_strip_culling(bool on) { g_strip_culling = on; }
void stats_reset() { memset(&g_stats, 0, sizeof g_stats); }
const Stats& stats() { return g_stats; }

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
    c->tan_y = tanf(fov_y_deg * 0.5f * 3.14159265f / 180.0f);
    c->tan_x = c->tan_y * (640.0f / 480.0f);
    c->focal = 240.0f / c->tan_y;
    c->near_w = 0.5f;                          // keeps every z = 1/W <= 2 (HUD sits at z >= 7)
    // proj_view rows: X = f * x_view, Y = -f * y_view (screen y down), W = -z_view
    for (int j = 0; j < 4; ++j) {
        c->proj_view.m[0][j] = c->focal * c->view.m[0][j];
        c->proj_view.m[1][j] = -c->focal * c->view.m[1][j];
        c->proj_view.m[2][j] = -c->view.m[2][j];
    }
}

// ---------------------------------------------------------------------------- models
const int MAX_XV = 4096;
// Transformed vertex, laid out like pvr_vertex_t from x to argb, so strip emission is a 24-byte copy.
struct XV { float x, y, z, u, v; uint32_t argb; };
typedef uint32_t __attribute__((may_alias)) u32a;   // word copies of float data without aliasing UB
static XV g_xv[MAX_XV];
struct XT { float X, Y, W, ndl; };           // first-bone partial result for two-bone groups
static XT g_xt[MAX_XV];

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
        float l = w.m[0][j] * w.m[0][j] + w.m[1][j] * w.m[1][j] + w.m[2][j] * w.m[2][j];
        if (l > s) s = l;
    }
    return sqrtf(s);
}

// Bounding sphere vs the view frustum. Returns 0 = outside, 1 = inside, -1 = crosses the near plane.
static int sphere_vis(const Camera& cam, Vec3 centre_world, float r) {
    Vec3 c = mat_point(cam.view, centre_world);
    float depth = -c.z;
    if (depth + r < cam.near_w) return 0;
    const float kx = 1.0f / sqrtf(1.0f + cam.tan_x * cam.tan_x), ky = 1.0f / sqrtf(1.0f + cam.tan_y * cam.tan_y);
    if ((fabsf(c.x) - depth * cam.tan_x) * kx > r) return 0;
    if ((fabsf(c.y) - depth * cam.tan_y) * ky > r) return 0;
    return depth - r < cam.near_w ? -1 : 1;
}

// Bounding spheres of every strip of the static models (computed once at load).
const int MAX_STRIP_SPHERES = 16384;
static float g_strip_sph[MAX_STRIP_SPHERES * 4];
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
                const float dx = v.x - o[0], dy = v.y - o[1], dz = v.z - o[2];
                const float d2 = dx * dx + dy * dy + dz * dz;
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
    for (int k = 0; k < m.num_mats; ++k) {
        int f = 0;
        if (m.mat_flags[k] & dmf::MAT_BILINEAR) f |= eng::MAT_BILINEAR;
        if (m.mat_flags[k] & dmf::MAT_ALPHA) f |= eng::MAT_TRANSLUCENT;
        g->doubled[k] = eng::material_create(m.mat_tex[k], f);
        g->single[k] = cull ? eng::material_create(m.mat_tex[k], f | eng::MAT_CULL) : g->doubled[k];
    }
    return true;
}

// Copies the strips of every batch from g_xv into the vertex arena (one arena batch per model batch).
// cull_offscreen: strips lying wholly beyond one screen edge are not submitted (props that are
// only partly in view - a house at the top of the screen - would otherwise send all their strips
// to the TA). Costs a few compares per vertex; used for the static props.
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
            if (cull_offscreen) {
                int l = 0, r = 0, t = 0, b = 0;
                for (int j = 0; j < len; ++j) {
                    const XV& x = g_xv[w[j]];
                    l += x.x < 0.0f; r += x.x > 640.0f; t += x.y < 0.0f; b += x.y > 480.0f;
                }
                if (l == len || r == len || t == len || b == len) { w += len; continue; }
            }
            tris += len - 2;
            for (int j = 0; j < len; ++j, ++o) {
                // x, y, z, u, v, argb are in the same order in XV and Vert3: six 32-bit moves
                // (integer moves have displacement addressing on the SH4; float stores do not)
                const u32a* s = reinterpret_cast<const u32a*>(&g_xv[w[j]]);
                u32a* d = reinterpret_cast<u32a*>(o);
                d[0] = eng::VERT;
                d[1] = s[0]; d[2] = s[1]; d[3] = s[2]; d[4] = s[3]; d[5] = s[4]; d[6] = s[5];
                d[7] = 0;
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
    const dmf::Model& m = *g.m;
    ++g_stats.models;
    const int nv = m.h->num_verts;
    if (nv > MAX_XV) return false;
    // animated limbs can leave the bind-pose sphere a little: 25 % margin
    int vis = sphere_vis(cam, mat_point(world, m.centre), m.radius * max_scale(world) * 1.25f);
    if (vis <= 0) { ++g_stats.models_culled; return false; }   // -1: too close to clip per-triangle; skip

    const int nb = skin ? m.h->num_bones : 0;
    Mat34 xf[dmf::MAX_BONES + 1];
    Vec3  lb[dmf::MAX_BONES + 1];
    const float n_scale = 1.0f / 127.0f;
    if (nb == 0) {
        xf[0] = mat_mul(cam.proj_view, world);
        lb[0] = normalize(mat_dir_t(world, light.dir)) * n_scale;
    } else {
        for (int b = 0; b < nb; ++b) {
            Mat34 ws = mat_mul(world, skin[b]);
            xf[b] = mat_mul(cam.proj_view, ws);
            lb[b] = normalize(mat_dir_t(ws, light.dir)) * n_scale;
        }
    }
    // Viewport offset folded into every bone matrix (sx = cx + X/W = (X + cx W) / W): the loops
    // below then need two float registers and two multiply-adds less per vertex.
    for (int b = 0; b < (nb ? nb : 1); ++b)
        for (int c = 0; c < 4; ++c) { xf[b].m[0][c] += cam.cx * xf[b].m[2][c]; xf[b].m[1][c] += cam.cy * xf[b].m[2][c]; }
    const float tn[4] = { (float)(tint >> 24) / 255.0f, (float)((tint >> 16) & 255) / 255.0f,
                          (float)((tint >> 8) & 255) / 255.0f, (float)(tint & 255) / 255.0f };
    // One base colour for the whole model (true for the KayKit Knight and the placeholders):
    // shade through a 256-entry table instead of 4 multiplies + 4 float->int casts per vertex.
    static uint32_t lut[256];
    const bool use_lut = m.uniform && nv > 256;
    if (use_lut)
        for (int i = 0; i < 256; ++i) lut[i] = shade(m.uniform_argb, (float)i * (1.0f / 255.0f), tn);
    const float amb = light.ambient, dif = light.diffuse;
    const float near_w = cam.near_w;

    // Finishes one vertex: perspective divide (screen mapping is in the matrix), lighting.
    auto finish = [&](XV* o, const dmf::Vertex* v, float X, float Y, float W, float ndl) {
        if (W < near_w) W = near_w;                  // cannot happen after the sphere test; belt and braces
        const float inv = 1.0f / W;
        o->x = X * inv;
        o->y = Y * inv;
        o->z = inv;
        o->u = v->u;
        o->v = v->v;
        float lit = amb + (ndl > 0.0f ? dif * ndl : 0.0f);
        // Ordered compare only (NaN -> 1.0): on the SH4 a negated `<=` needs an unordered test,
        // which GCC's default -mieee code does with a soft-float __unordsf2 call.
        lit = lit < 1.0f ? lit : 1.0f;
        o->argb = use_lut ? lut[(int)(lit * 255.0f)] : shade(v->argb, lit, tn);
    };
    // One matrix live per loop: the SH4 has 16 float registers and a 3x4 matrix plus the light
    // vector already takes 15, so two-bone groups run two single-matrix passes (A, then B + blend)
    // instead of one loop that would spill both matrices to the stack on every vertex.
    for (int gi = 0; gi < m.h->num_groups; ++gi) {
        const dmf::Group& gr = m.groups[gi];
        const int b0 = nb ? gr.bone0 : 0;
        const Mat34 A = xf[b0];
        const Vec3 la = lb[b0];
        const dmf::Vertex* v = m.verts + gr.first;
        XV* o = g_xv + gr.first;
        if (prelit && !(nb && gr.bone1 != dmf::NO_BONE)) {
            const uint32_t* pc = prelit + gr.first;
            for (int i = 0; i < gr.count; ++i, ++v, ++o) {
                const float px = v->x, py = v->y, pz = v->z;
                const float X = A.m[0][0] * px + A.m[0][1] * py + A.m[0][2] * pz + A.m[0][3];
                const float Y = A.m[1][0] * px + A.m[1][1] * py + A.m[1][2] * pz + A.m[1][3];
                float W = A.m[2][0] * px + A.m[2][1] * py + A.m[2][2] * pz + A.m[2][3];
                if (W < near_w) W = near_w;
                const float inv = 1.0f / W;
                o->x = X * inv; o->y = Y * inv; o->z = inv;
                u32a* d = reinterpret_cast<u32a*>(&o->u);
                const u32a* uv = reinterpret_cast<const u32a*>(&v->u);
                d[0] = uv[0]; d[1] = uv[1]; d[2] = pc[i];
            }
            continue;
        }
        if (!(nb && gr.bone1 != dmf::NO_BONE)) {
            for (int i = 0; i < gr.count; ++i, ++v, ++o) {
                const float px = v->x, py = v->y, pz = v->z;
                finish(o, v,
                       A.m[0][0] * px + A.m[0][1] * py + A.m[0][2] * pz + A.m[0][3],
                       A.m[1][0] * px + A.m[1][1] * py + A.m[1][2] * pz + A.m[1][3],
                       A.m[2][0] * px + A.m[2][1] * py + A.m[2][2] * pz + A.m[2][3],
                       v->nx * la.x + v->ny * la.y + v->nz * la.z);
            }
            continue;
        }
        if (prelit) {                                            // two bones, baked colours: no lighting
            XT* t = g_xt + gr.first;
            for (int i = 0; i < gr.count; ++i, ++v, ++t) {      // pass 1: bone0
                const float px = v->x, py = v->y, pz = v->z;
                t->X = A.m[0][0] * px + A.m[0][1] * py + A.m[0][2] * pz + A.m[0][3];
                t->Y = A.m[1][0] * px + A.m[1][1] * py + A.m[1][2] * pz + A.m[1][3];
                t->W = A.m[2][0] * px + A.m[2][1] * py + A.m[2][2] * pz + A.m[2][3];
            }
            const Mat34 B = xf[gr.bone1];
            v = m.verts + gr.first;
            t = g_xt + gr.first;
            const uint32_t* pc = prelit + gr.first;
            for (int i = 0; i < gr.count; ++i, ++v, ++t, ++o) { // pass 2: bone1, blend by w0
                const float px = v->x, py = v->y, pz = v->z;
                const float w0 = (float)v->w0 * (1.0f / 255.0f);
                const float X1 = B.m[0][0] * px + B.m[0][1] * py + B.m[0][2] * pz + B.m[0][3];
                const float Y1 = B.m[1][0] * px + B.m[1][1] * py + B.m[1][2] * pz + B.m[1][3];
                const float W1 = B.m[2][0] * px + B.m[2][1] * py + B.m[2][2] * pz + B.m[2][3];
                const float X = X1 + (t->X - X1) * w0, Y = Y1 + (t->Y - Y1) * w0;
                float W = W1 + (t->W - W1) * w0;
                if (W < near_w) W = near_w;
                const float inv = 1.0f / W;
                o->x = X * inv; o->y = Y * inv; o->z = inv;
                u32a* d = reinterpret_cast<u32a*>(&o->u);
                const u32a* uv = reinterpret_cast<const u32a*>(&v->u);
                d[0] = uv[0]; d[1] = uv[1]; d[2] = pc[i];
            }
            continue;
        }
        XT* t = g_xt + gr.first;
        for (int i = 0; i < gr.count; ++i, ++v, ++t) {          // pass 1: bone0
            const float px = v->x, py = v->y, pz = v->z;
            t->X = A.m[0][0] * px + A.m[0][1] * py + A.m[0][2] * pz + A.m[0][3];
            t->Y = A.m[1][0] * px + A.m[1][1] * py + A.m[1][2] * pz + A.m[1][3];
            t->W = A.m[2][0] * px + A.m[2][1] * py + A.m[2][2] * pz + A.m[2][3];
            t->ndl = v->nx * la.x + v->ny * la.y + v->nz * la.z;
        }
        const Mat34 B = xf[gr.bone1];
        const Vec3 lbb = lb[gr.bone1];
        v = m.verts + gr.first;
        t = g_xt + gr.first;
        for (int i = 0; i < gr.count; ++i, ++v, ++t, ++o) {    // pass 2: bone1, blend by w0
            const float px = v->x, py = v->y, pz = v->z;
            const float w0 = (float)v->w0 * (1.0f / 255.0f);
            const float X1 = B.m[0][0] * px + B.m[0][1] * py + B.m[0][2] * pz + B.m[0][3];
            const float Y1 = B.m[1][0] * px + B.m[1][1] * py + B.m[1][2] * pz + B.m[1][3];
            const float W1 = B.m[2][0] * px + B.m[2][1] * py + B.m[2][2] * pz + B.m[2][3];
            const float n1 = v->nx * lbb.x + v->ny * lbb.y + v->nz * lbb.z;
            finish(o, v, X1 + (t->X - X1) * w0, Y1 + (t->Y - Y1) * w0, W1 + (t->W - W1) * w0, n1 + (t->ndl - n1) * w0);
        }
    }
    g_stats.verts_xf += nv;

    return emit_batches(g);
}

// ---------------------------------------------------------------------------- polygons
struct CV { float X, Y, W, u, v; float c[4]; };

static CV to_cv(const Camera& cam, const PolyVert& p) {
    Vec3 s = mat_point(cam.proj_view, p.p);
    CV r;
    r.X = s.x; r.Y = s.y; r.W = s.z; r.u = p.u; r.v = p.v;
    r.c[0] = (float)(p.argb >> 24); r.c[1] = (float)((p.argb >> 16) & 255);
    r.c[2] = (float)((p.argb >> 8) & 255); r.c[3] = (float)(p.argb & 255);
    return r;
}

static CV cv_lerp(const CV& a, const CV& b, float t) {
    CV r;
    r.X = a.X + (b.X - a.X) * t; r.Y = a.Y + (b.Y - a.Y) * t; r.W = a.W + (b.W - a.W) * t;
    r.u = a.u + (b.u - a.u) * t; r.v = a.v + (b.v - a.v) * t;
    for (int i = 0; i < 4; ++i) r.c[i] = a.c[i] + (b.c[i] - a.c[i]) * t;
    return r;
}

int emit_poly(const Camera& cam, const PolyVert* in, int n, eng::Vert3* out) {
    if (n < 3 || n > 8) return 0;
    CV a[8], b[16];
    int na = 0, inside = 0;
    for (int i = 0; i < n; ++i) { a[na] = to_cv(cam, in[i]); inside += a[na].W >= cam.near_w; ++na; }
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
        const float inv = 1.0f / c.W;
        eng::Vert3& o = out[k];
        o.flags = k == np - 1 ? eng::VERT_EOL : eng::VERT;
        o.x = cam.cx + c.X * inv; o.y = cam.cy + c.Y * inv; o.z = inv;
        o.u = c.u; o.v = c.v;
        o.argb = ((uint32_t)(c.c[0] + 0.5f) << 24) | ((uint32_t)(c.c[1] + 0.5f) << 16) |
                 ((uint32_t)(c.c[2] + 0.5f) << 8) | (uint32_t)(c.c[3] + 0.5f);
        o.oargb = 0;
    }
    g_stats.verts_out += np;
    g_stats.tris += np - 2;
    return np;
}

// ---------------------------------------------------------------------------- static props
void prelight(const dmf::Model& m, const Mat34& world, const Light& light, uint32_t* colors) {
    const Vec3 l = normalize(mat_dir_t(world, light.dir)) * (1.0f / 127.0f);     // light in model space
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (int i = 0; i < m.h->num_verts; ++i) {
        const dmf::Vertex& v = m.verts[i];
        const float ndl = v.nx * l.x + v.ny * l.y + v.nz * l.z;
        float lit = light.ambient + (ndl > 0.0f ? light.diffuse * ndl : 0.0f);
        lit = lit < 1.0f ? lit : 1.0f;
        colors[i] = shade(v.argb, lit, white);
    }
}

// Model-space bounding box vs the view frustum: 0 = outside (all 8 corners beyond one plane),
// 1 = in front of the near plane (possibly crossing a screen edge), 2 = wholly on screen,
// -1 = crosses the near plane. Much tighter than the sphere for tall, wide
// props (a house's sphere still touched the view when the house was well above the screen).
// edges: which screen edges the box crosses (1 left, 2 right, 4 top, 8 bottom), for vis == 1
static int box_vis(const Camera& cam, const dmf::Model& m, const Mat34& world, int* edges = nullptr) {
    const Mat34 vw = mat_mul(cam.view, world);
    int out_l = 0, out_r = 0, out_t = 0, out_b = 0, behind = 0;
    for (int k = 0; k < 8; ++k) {
        const Vec3 p = v3((k & 1) ? m.h->bmax[0] : m.h->bmin[0], (k & 2) ? m.h->bmax[1] : m.h->bmin[1],
                          (k & 4) ? m.h->bmax[2] : m.h->bmin[2]);
        const Vec3 c = mat_point(vw, p);
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
    const Mat34 rot = mat_trs_y(v3(0, 0, 0), yaw, 1, 1, 1);
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (int gi = 0; gi < m.h->num_groups; ++gi) {
        const dmf::Group& gr = m.groups[gi];
        const bool two = gr.bone1 != dmf::NO_BONE;
        const Mat34 a = skin ? mat_mul(rot, skin[gr.bone0]) : rot;
        const Mat34 b = (skin && two) ? mat_mul(rot, skin[gr.bone1]) : a;
        for (int i = gr.first; i < gr.first + gr.count; ++i) {
            const dmf::Vertex& v = m.verts[i];
            const Vec3 n = v3((float)v.nx, (float)v.ny, (float)v.nz);
            const float w0 = two ? (float)v.w0 * (1.0f / 255.0f) : 1.0f;
            const Vec3 nw = normalize(mat_dir(a, n) * w0 + mat_dir(b, n) * (1.0f - w0));
            const float ndl = dot(nw, light.dir);
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
    const Mat34 A = mat_mul(cam.proj_view, world);
    const float near_w = cam.near_w, cx = cam.cx, cy = cam.cy;
    if (vis > 0) {
        // Wholly in front of the near plane (the usual case): transform each strip vertex straight
        // into the vertex arena - props share few vertices between strips (~1.2 emissions per
        // vertex), so this beats transforming into a scratch buffer and copying. Each strip's
        // bounding sphere is tested first (~20 instructions per strip): strips off screen cost
        // neither transforms nor TA bandwidth (half-visible houses at the screen edges).
        // Screen x in [0, 640] <=> |X| <= 320 W (f tan_x = 320); y likewise with 240.
        const float* sph = (vis == 2 || !g_strip_culling) ? nullptr : g.strip_spheres;   // wholly on screen: no tests
        // Viewport offset folded into the matrix (sx = cx + X/W = (X + cx W) / W): two float
        // registers and two multiply-adds less per vertex in the loop below.
        Mat34 S = A;
        for (int c = 0; c < 4; ++c) { S.m[0][c] += cx * A.m[2][c]; S.m[1][c] += cy * A.m[2][c]; }
        const dmf::Vertex* const verts = m.verts;        // locals: the stores below cannot alias them
        const uint32_t* const cols = colors;
        const float ws = max_scale(world);
        const float kx = cam.focal * sqrtf(1.0f + cam.tan_x * cam.tan_x) * ws;
        const float ky = cam.focal * sqrtf(1.0f + cam.tan_y * cam.tan_y) * ws;
        // Strip test planes: only the screen edges the model's box crosses (a strip cannot be wholly
        // beyond an edge that every box corner is inside of). Each plane is a row combination of A
        // in model space: a strip is culled if plane . (centre, 1) > radius * k. Usually one edge.
        // Screen x in [0, 640] <=> |X| <= 320 W (f tan_x = 320); y likewise with 240.
        float pl[4][5];
        int np = 0;
        for (int e = 0; e < 4; ++e) {
            if (!(edges & (1 << e))) continue;
            const int row = e < 2 ? 0 : 1;                             // 0 left, 1 right, 2 top, 3 bottom
            const float sign = (e == 0 || e == 2) ? -1.0f : 1.0f, half = e < 2 ? 320.0f : 240.0f;
            for (int c = 0; c < 4; ++c) pl[np][c] = sign * A.m[row][c] - half * A.m[2][c];
            pl[np][4] = e < 2 ? kx : ky;
            ++np;
        }
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
                if (sph) {
                    const float sx = sph[0], sy = sph[1], sz = sph[2], sr = sph[3];
                    bool off = false;
                    for (int q = 0; q < np && !off; ++q)
                        off = pl[q][0] * sx + pl[q][1] * sy + pl[q][2] * sz + pl[q][3] > sr * pl[q][4];
                    if (off) {
                        w += len;
                        sph += 4;
                        ++g_stats.strips_culled;
                        continue;
                    }
                    sph += 4;
                }
                g_stats.verts_xf += len;
                tris += len - 2;
                for (int j = 0; j < len; ++j, ++o) {
                    const int idx = w[j];
                    const dmf::Vertex* v = verts + idx;
                    const float px = v->x, py = v->y, pz = v->z;
                    const float X = S.m[0][0] * px + S.m[0][1] * py + S.m[0][2] * pz + S.m[0][3];
                    const float Y = S.m[1][0] * px + S.m[1][1] * py + S.m[1][2] * pz + S.m[1][3];
                    const float W = S.m[2][0] * px + S.m[2][1] * py + S.m[2][2] * pz + S.m[2][3];
                    const float inv = 1.0f / W;
                    o->x = X * inv; o->y = Y * inv; o->z = inv;
                    // flags, u, v, colours: integer moves (displacement addressing on the SH4)
                    u32a* d = reinterpret_cast<u32a*>(o);
                    const u32a* uv = reinterpret_cast<const u32a*>(&v->u);
                    d[0] = eng::VERT; d[4] = uv[0]; d[5] = uv[1]; d[6] = cols[idx]; d[7] = 0;
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
        return true;
    }
    const dmf::Vertex* v = m.verts;
    XV* o = g_xv;
    XT* t = g_xt;                                       // keeps W for the near-plane path
    for (int i = 0; i < nv; ++i, ++v, ++o, ++t) {
        const float px = v->x, py = v->y, pz = v->z;
        const float X = A.m[0][0] * px + A.m[0][1] * py + A.m[0][2] * pz + A.m[0][3];
        const float Y = A.m[1][0] * px + A.m[1][1] * py + A.m[1][2] * pz + A.m[1][3];
        const float W = A.m[2][0] * px + A.m[2][1] * py + A.m[2][2] * pz + A.m[2][3];
        t->W = W;
        const float inv = 1.0f / (W < near_w ? near_w : W);
        o->x = cx + X * inv;
        o->y = cy + Y * inv;
        o->z = inv;
        o->u = v->u;
        o->v = v->v;
        o->argb = colors[i];
    }
    g_stats.verts_xf += nv;
    ++g_stats.statics_near;
    // Crosses the near plane (rare: a prop right under the camera). Triangles wholly in front go
    // out as they are; the others are clipped. Nothing pops at the screen edge.
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
                if (j & 1) { int tmp = a; a = b; b = tmp; }     // strip parity: keep the winding
                if (g_xt[a].W >= near_w && g_xt[b].W >= near_w && g_xt[c].W >= near_w) {
                    const int idx[3] = { a, b, c };
                    for (int q = 0; q < 3; ++q) {
                        const XV& x = g_xv[idx[q]];
                        eng::Vert3& d = out[used + q];
                        d.flags = q == 2 ? eng::VERT_EOL : eng::VERT;
                        d.x = x.x; d.y = x.y; d.z = x.z; d.u = x.u; d.v = x.v; d.argb = x.argb; d.oargb = 0;
                    }
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
    }
    return true;
}

void draw_blob_shadow(const Camera& cam, eng::Material mat, Vec3 c, float radius, int alpha) {
    const int SEG = 12;
    // unit-circle table (computed once): no trig per shadow
    static float cs[SEG + 1], sn[SEG + 1];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i <= SEG; ++i) { cs[i] = cosf(6.28318531f * (float)i / SEG); sn[i] = sinf(6.28318531f * (float)i / SEG); }
        ready = true;
    }
    eng::Vert3* out = eng::vtx_begin(SEG * 5);
    if (!out) return;
    const uint32_t centre_col = (uint32_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha) << 24;
    const float y = 0.02f;                          // just above the ground plane
    // transform the centre + ring once; a shadow close enough to cross the near plane falls back
    // to the clipper triangle by triangle
    struct P { float x, y, z; bool front; } p[SEG + 1];
    Vec3 s = mat_point(cam.proj_view, v3(c.x, y, c.z));
    bool all_front = s.z >= cam.near_w;
    p[SEG] = P{ cam.cx + s.x / s.z, cam.cy + s.y / s.z, 1.0f / s.z, all_front };
    for (int i = 0; i < SEG; ++i) {
        s = mat_point(cam.proj_view, v3(c.x + radius * cs[i], y, c.z + radius * sn[i]));
        const bool f = s.z >= cam.near_w;
        all_front = all_front && f;
        if (f) p[i] = P{ cam.cx + s.x / s.z, cam.cy + s.y / s.z, 1.0f / s.z, true };
    }
    int used = 0;
    for (int i = 0; i < SEG; ++i) {
        const int a = (i + 1) % SEG, b = i;        // centre, ring(i+1), ring(i): counter-clockwise from above
        if (all_front) {
            const P* t[3] = { &p[SEG], &p[a], &p[b] };
            for (int k = 0; k < 3; ++k) {
                eng::Vert3& o = out[used + k];
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
