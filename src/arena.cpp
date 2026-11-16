// arena.cpp - see arena.h
#include "arena.h"
#include "dmf.h"
#include "models.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace bt {

using namespace r3d;

// Prelit colours of every arena prop (all arenas share the pool; loaded once).
const int ARENA_COLOR_POOL = 40 * 1024;
static uint32_t g_pool[ARENA_COLOR_POOL];
static int g_pool_used = 0;
static eng::Material g_grass_mat = 0, g_dirt_mat = 0, g_dirt_rim_mat = 0;

static bool next_line(const char*& p, char* line, int cap) {
    if (!*p) return false;
    int n = 0;
    while (*p && *p != '\n') { if (n < cap - 1) line[n++] = *p; ++p; }
    if (*p == '\n') ++p;
    line[n] = 0;
    return true;
}

static uint32_t hex_rgb(const char* s) { return (uint32_t)strtoul(s, nullptr, 16) & 0xFFFFFFu; }

uint32_t arena_light_color(const Arena& a, uint32_t c) {
    float r = (float)((c >> 16) & 255) * a.light_rgb[0], g = (float)((c >> 8) & 255) * a.light_rgb[1];
    float b = (float)(c & 255) * a.light_rgb[2];
    uint32_t ri = r < 255.0f ? (uint32_t)r : 255u, gi = g < 255.0f ? (uint32_t)g : 255u, bi = b < 255.0f ? (uint32_t)b : 255u;
    return (c & 0xFF000000u) | (ri << 16) | (gi << 8) | bi;
}

bool arena_load(const char* path, Arena* A) {
    static char buf[8192];
    char msg[160];
    memset(A, 0, sizeof *A);
    A->light = default_light();
    A->light_rgb[0] = A->light_rgb[1] = A->light_rgb[2] = 1.0f;
    A->sky = 0x6F8FC0; A->horizon = 0xA9C2D8;
    A->clear_r = 5.6f; A->rim = 1.6f;
    if (eng::file_read(path, buf, sizeof buf) < 0) {
        snprintf(msg, sizeof msg, "arena: cannot read %s", path);
        eng::dbg_print(msg);
        return false;
    }
    if (!g_grass_mat) {
        g_grass_mat = eng::material_create(dmf::shared_texture("/rd/tex/ground.png", false), eng::MAT_BILINEAR);
        const eng::Sprite dirt = dmf::shared_texture("/rd/tex/dirt.png", false);
        g_dirt_mat = eng::material_create(dirt, eng::MAT_BILINEAR);
        g_dirt_rim_mat = eng::material_create(dirt, eng::MAT_BILINEAR | eng::MAT_TRANSLUCENT);
    }
    const char* p = buf;
    char line[256];
    int ln = 0;
    while (next_line(p, line, sizeof line)) {
        ++ln;
        char* c = line;
        while (*c == ' ' || *c == '\t') ++c;
        if (!*c || *c == '#' || *c == '\r') continue;
        char kw[16] = { 0 };
        int used = 0;
        if (sscanf(c, "%15s%n", kw, &used) != 1) continue;
        const char* rest = c + used;
        bool bad = false;
        if (!strcmp(kw, "name")) {
            bad = sscanf(rest, "%15s", A->name) != 1;
        } else if (!strcmp(kw, "light")) {
            bad = sscanf(rest, "%f %f %f %f %f", &A->light.ambient, &A->light.diffuse, &A->light_rgb[0], &A->light_rgb[1], &A->light_rgb[2]) != 5;
        } else if (!strcmp(kw, "sky")) {
            char s1[16] = { 0 }, s2[16] = { 0 }, s3[16] = { 0 };
            const int n = sscanf(rest, "%15s %15s %15s", s1, s2, s3);
            bad = n < 1;
            if (!bad) { A->sky = hex_rgb(s1); A->horizon = n == 3 && !strcmp(s2, "horizon") ? hex_rgb(s3) : A->sky; }
        } else if (!strcmp(kw, "ground")) {
            char g[16] = { 0 };
            bad = sscanf(rest, "%15s %f %f", g, &A->clear_r, &A->rim) != 3;
            A->ground = !strcmp(g, "dirt") ? 1 : !strcmp(g, "stone") ? 2 : 0;
        } else if (!strcmp(kw, "prop")) {
            char model[24] = { 0 };
            float x, z, rot;
            int n2 = 0;
            bad = sscanf(rest, "%23s %f %f %f%n", model, &x, &z, &rot, &n2) != 4;
            float scale = 1.0f;
            uint32_t tint = 0xFFFFFF;
            if (!bad) {
                const char* opt = rest + n2;
                char k[16]; char v[16]; int n3 = 0;
                while (sscanf(opt, "%15s %15s%n", k, v, &n3) == 2) {
                    if (!strcmp(k, "scale")) scale = (float)atof(v);
                    else if (!strcmp(k, "tint")) tint = hex_rgb(v);
                    else { bad = true; break; }
                    opt += n3;
                }
            }
            if (!bad && A->num_props < ARENA_MAX_PROPS) {
                const int mi = models_get(model);
                if (mi < 0) {
                    snprintf(msg, sizeof msg, "arena %s:%d: model '%s' missing", path, ln, model);
                    eng::dbg_print(msg);
                    continue;
                }
                const dmf::Model& m = models_at(mi).m;
                ArenaProp& pr = A->props[A->num_props];
                pr.model = mi;
                pr.world = mat_trs_y(v3(x, 0.0f, z), rot * (3.14159265f / 180.0f), scale, scale, scale);
                pr.x = x; pr.z = z;
                if (g_pool_used + m.h->num_verts > ARENA_COLOR_POOL) { eng::dbg_print("arena: colour pool full"); continue; }
                uint32_t* col = g_pool + g_pool_used;
                g_pool_used += m.h->num_verts;
                prelight(m, pr.world, A->light, col);
                const float tr = (float)((tint >> 16) & 255) / 255.0f, tg = (float)((tint >> 8) & 255) / 255.0f, tb = (float)(tint & 255) / 255.0f;
                const float save[3] = { A->light_rgb[0], A->light_rgb[1], A->light_rgb[2] };
                A->light_rgb[0] *= tr; A->light_rgb[1] *= tg; A->light_rgb[2] *= tb;
                for (int i = 0; i < m.h->num_verts; ++i) col[i] = arena_light_color(*A, col[i]);
                A->light_rgb[0] = save[0]; A->light_rgb[1] = save[1]; A->light_rgb[2] = save[2];
                pr.colors = col;
                for (int k = 0; k < m.h->num_batches; ++k) A->prop_tris += (int)m.batches[k].num_tris;
                ++A->num_props;
            }
        } else if (!strcmp(kw, "hero")) {
            bad = sscanf(rest, "%f %f", &A->hero_x, &A->hero_z) != 2;
        } else if (!strcmp(kw, "slots")) {
            int n = 0, off = 0;
            bad = sscanf(rest, "%d%n", &n, &off) != 1 || n < 1 || n > 4;
            const char* q = rest + off;
            for (int i = 0; !bad && i < n; ++i) {
                int o2 = 0;
                bad = sscanf(q, "%f %f%n", &A->slot_x[n - 1][i], &A->slot_z[n - 1][i], &o2) != 2;
                q += o2;
            }
        } else if (!strcmp(kw, "shot")) {
            if (A->num_shots < ARENA_MAX_SHOTS) {
                ArenaShot& s = A->shots[A->num_shots];
                bad = sscanf(rest, "%15s %f %f %f %f %f %f %f", s.name, &s.eye.x, &s.eye.y, &s.eye.z, &s.target.x, &s.target.y,
                             &s.target.z, &s.fov) != 8;
                if (!bad) ++A->num_shots;
            }
        } else {
            bad = true;
        }
        if (bad) {
            snprintf(msg, sizeof msg, "arena %.40s:%d: bad line %.80s", path, ln, c);
            eng::dbg_print(msg);
            return false;
        }
    }
    for (int k = 0; k < 3; ++k) A->light_mul[k] = (uint32_t)(A->light_rgb[k] * 256.0f + 0.5f);
    A->ok = arena_shot(*A, "overview") != nullptr;
    snprintf(msg, sizeof msg, "arena %s: %d props (%d triangles), %d shots, %d/%d prelit colours", A->name, A->num_props,
             A->prop_tris, A->num_shots, g_pool_used, ARENA_COLOR_POOL);
    eng::dbg_print(msg);
    return A->ok;
}

const ArenaShot* arena_shot(const Arena& a, const char* name) {
    for (int i = 0; i < a.num_shots; ++i)
        if (!strcmp(a.shots[i].name, name)) return &a.shots[i];
    return nullptr;
}

// ---------------------------------------------------------------------------- ground
static uint32_t ground_color(const Arena& a, uint32_t alpha) {
    // flat ground facing up: ambient + diffuse * (light . up)
    float lit = a.light.ambient + (a.light.dir.y > 0.0f ? a.light.diffuse * a.light.dir.y : 0.0f);
    lit = lit < 1.0f ? lit : 1.0f;
    const uint32_t l = (uint32_t)(lit * 255.0f);
    return arena_light_color(a, (alpha << 24) | (l << 16) | (l << 8) | l);
}

// Ground points, transformed once per frame as one XMTRX batch (one FTRV + one FSRRA each) and
// shared by the quads around them.
struct GPt { float x, y, z; bool front; };
const int GRID_X0 = -8, GRID_X1 = 8, GRID_Z0 = -8, GRID_Z1 = 4;   // 3 x 3 cells: x -24..24, z -24..12
const float GRID_CELL = 3.0f;
const int GNX = GRID_X1 - GRID_X0 + 1, GNZ = GRID_Z1 - GRID_Z0 + 1;
const int SEG = 24, CORE_RINGS = 3;                                 // dirt: 3 core rings + 2 rim rings
const int NRING = CORE_RINGS + 2;                                    // ring radii index 1..NRING (0 = centre)

static void transform(const Camera& cam, const Vec3* p, GPt* out, int n) {
    xmtrx_load(cam.screen);
    for (int i = 0; i < n; ++i) {
        const Vec3 s = xmtrx_point(p[i]);                             // (X', Y', W)
        out[i].front = s.z >= cam.near_w;
        const float inv = shz_invf_fsrra(out[i].front ? s.z : cam.near_w);
        out[i].x = s.x * inv; out[i].y = s.y * inv; out[i].z = inv;
    }
}

// One quad from four transformed corners (counter-clockwise seen from above); corners behind the
// near plane send it through the clipper (from the world positions).
static void quad(const Camera& cam, eng::Vert3* out, int& used, const GPt* g[4], const Vec3* w[4],
                 const float* u, const float* v, const uint32_t* c) {
    if (g[0]->front && g[1]->front && g[2]->front && g[3]->front) {
        // off screen? (plain compares; fminf/fmaxf are library calls on the SH4)
        if ((g[0]->x < 0 && g[1]->x < 0 && g[2]->x < 0 && g[3]->x < 0) || (g[0]->x > 640 && g[1]->x > 640 && g[2]->x > 640 && g[3]->x > 640) ||
            (g[0]->y < 0 && g[1]->y < 0 && g[2]->y < 0 && g[3]->y < 0) || (g[0]->y > 480 && g[1]->y > 480 && g[2]->y > 480 && g[3]->y > 480)) return;
        static const int order[4] = { 0, 1, 3, 2 };                   // loop a b c d -> strip a b d c
        for (int k = 0; k < 4; ++k) {
            const int j = order[k];
            eng::Vert3& o = out[used + k];
            shz_dcache_alloc_line(&o);                                // whole 32-byte line written below
            o.flags = k == 3 ? eng::VERT_EOL : eng::VERT;
            o.x = g[j]->x; o.y = g[j]->y; o.z = g[j]->z; o.u = u[j]; o.v = v[j]; o.argb = c[j]; o.oargb = 0;
        }
        used += 4;
        stats_add(4, 2);
    } else if (g[0]->front || g[1]->front || g[2]->front || g[3]->front) {
        PolyVert q[4];
        for (int k = 0; k < 4; ++k) { q[k].p = *w[k]; q[k].u = u[k]; q[k].v = v[k]; q[k].argb = c[k]; }
        used += emit_poly(cam, q, 4, out + used);
    }
}

void arena_draw(const Arena& a, const Camera& cam, int* ground_verts) {
    const int vstart = stats().verts_out;
    static Vec3 gw[GNX * GNZ], dw[1 + NRING * SEG];
    static GPt gs[GNX * GNZ], ds[1 + NRING * SEG];
    static float r_of[NRING + 1];
    static float cs[SEG], sn[SEG];
    static bool ready = false;
    const float core = a.clear_r - a.rim;
    if (!ready) {
        for (int i = 0; i < SEG; ++i) { const shz_sincos_t r = shz_sincosf(6.28318531f * (float)i / SEG); cs[i] = r.cos; sn[i] = r.sin; }
        for (int z = 0; z < GNZ; ++z)
            for (int x = 0; x < GNX; ++x) gw[z * GNX + x] = v3(GRID_CELL * (float)(GRID_X0 + x), 0.0f, GRID_CELL * (float)(GRID_Z0 + z));
        ready = true;
    }
    // ring radii follow the arena (centre, core rings, rim middle, outer edge)
    r_of[0] = 0.0f;
    for (int r = 1; r <= CORE_RINGS; ++r) r_of[r] = core * (float)r / CORE_RINGS;
    r_of[CORE_RINGS + 1] = core + a.rim * 0.5f;
    r_of[CORE_RINGS + 2] = a.clear_r;
    const float Y = 0.012f;
    dw[0] = v3(0, Y, 0);
    for (int r = 1; r <= NRING; ++r)
        for (int i = 0; i < SEG; ++i) dw[1 + (r - 1) * SEG + i] = v3(r_of[r] * cs[i], Y, r_of[r] * sn[i]);
    transform(cam, gw, gs, GNX * GNZ);
    transform(cam, dw, ds, 1 + NRING * SEG);

    // -- grass (or the outer ground): one atlas cell of ground.png per 3 x 3 cell, skipped under the opaque clearing
    const float CELL_UV = 0.25f, INSET = 1.0f / 256.0f;
    const int cell = a.ground == 2 ? 2 : 0;              // atlas cell: 0 grass, 2 path/stone
    const float u0 = (float)(cell % 4) * CELL_UV + INSET, v0 = (float)(cell / 4) * CELL_UV + INSET;
    const float u1 = u0 + CELL_UV - 2 * INSET, v1 = v0 + CELL_UV - 2 * INSET;
    const uint32_t gc = ground_color(a, 255);
    eng::Vert3* out = eng::vtx_begin((GNX - 1) * (GNZ - 1) * 5);
    if (out) {
        int used = 0;
        const float qu[4] = { u0, u0, u1, u1 }, qv[4] = { v0, v1, v1, v0 };
        const uint32_t qc[4] = { gc, gc, gc, gc };
        for (int z = 0; z + 1 < GNZ; ++z)
            for (int x = 0; x + 1 < GNX; ++x) {
                const int i00 = z * GNX + x, i01 = (z + 1) * GNX + x, i11 = (z + 1) * GNX + x + 1, i10 = z * GNX + x + 1;
                bool inside = true;                                   // every corner inside the opaque core
                const int ids[4] = { i00, i01, i11, i10 };
                for (int k = 0; k < 4 && inside; ++k) inside = gw[ids[k]].x * gw[ids[k]].x + gw[ids[k]].z * gw[ids[k]].z < core * core;
                if (inside) continue;
                const GPt* g[4] = { &gs[i00], &gs[i01], &gs[i11], &gs[i10] };
                const Vec3* w[4] = { &gw[i00], &gw[i01], &gw[i11], &gw[i10] };
                quad(cam, out, used, g, w, qu, qv, qc);
            }
        eng::vtx_end(g_grass_mat, used);
    }
    // -- the dirt clearing: opaque core (centre fan + rings), then the translucent rim fading out
    const float UVS = 1.0f / 3.0f;                       // dirt.png repeats every 3 units
    auto ring_pt = [&](int r, int i) -> int { return r == 0 ? 0 : 1 + (r - 1) * SEG + (i % SEG); };
    auto dirt_quads = [&](int r0, int r1, eng::Material mat, const uint32_t* col_of_ring) {
        eng::Vert3* o = eng::vtx_begin((r1 - r0) * SEG * 5);
        if (!o) return;
        int used = 0;
        for (int r = r0; r < r1; ++r)
            for (int i = 0; i < SEG; ++i) {
                // counter-clockwise from above: inner i, outer i, outer i+1, inner i+1
                const int ids[4] = { ring_pt(r, i), ring_pt(r + 1, i), ring_pt(r + 1, i + 1), ring_pt(r, i + 1) };
                const GPt* g[4] = { &ds[ids[0]], &ds[ids[1]], &ds[ids[2]], &ds[ids[3]] };
                const Vec3* w[4] = { &dw[ids[0]], &dw[ids[1]], &dw[ids[2]], &dw[ids[3]] };
                float u[4], v[4];
                for (int k = 0; k < 4; ++k) { u[k] = w[k]->x * UVS; v[k] = w[k]->z * UVS; }
                const uint32_t c[4] = { col_of_ring[r], col_of_ring[r + 1], col_of_ring[r + 1], col_of_ring[r] };
                quad(cam, o, used, g, w, u, v, c);
            }
        eng::vtx_end(mat, used);
    };
    const uint32_t dc = ground_color(a, 255);
    const uint32_t core_cols[NRING + 1] = { dc, dc, dc, dc, dc, dc };
    const uint32_t rim_cols[NRING + 1] = { dc, dc, dc, dc, ground_color(a, 150), ground_color(a, 0) };
    dirt_quads(0, CORE_RINGS, g_dirt_mat, core_cols);
    dirt_quads(CORE_RINGS, NRING, g_dirt_rim_mat, rim_cols);
    if (ground_verts) *ground_verts = stats().verts_out - vstart;
    // -- props (prelit; draw_static culls by box, then per strip)
    for (int i = 0; i < a.num_props; ++i) {
        const ArenaProp& pr = a.props[i];
#ifdef ARENA_COST_DEBUG
        const int before = stats().verts_out;
#endif
        draw_static(cam, models_at(pr.model).gfx, pr.world, pr.colors);
#ifdef ARENA_COST_DEBUG
        char dbg[96];
        snprintf(dbg, sizeof dbg, "prop %d %s %.1f %.1f out %d", i, models_at(pr.model).name, pr.x, pr.z, stats().verts_out - before);
        eng::dbg_print(dbg);
#endif
    }
}

}
