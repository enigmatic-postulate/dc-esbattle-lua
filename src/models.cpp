// models.cpp - see models.h (moved out of field3d.cpp for slice 3D-4; behaviour unchanged)
#include "models.h"
#include <stdio.h>
#include <string.h>

namespace bt {

static LoadedModel g_models[MAX_MODELS];
static int g_nmodels = 0;

// Cel-shading prototype (docs/CEL_SHADING_STUDY.md): a character's decimated outline hull, romdisk/models/hull/<name>.dmf
// (tools/cel/hull_lab.py --write), loaded with the character when outlines are on. Never loaded otherwise.
const int MAX_HULLS = 10;
static dmf::Model g_hull_m[MAX_HULLS];
static r3d::ModelGfx g_hull_g[MAX_HULLS];
static int g_nhulls = 0;
static void load_hull(LoadedModel& lm) {
    static const char* const HAVE[] = { "knight", "wren", "brom", "corwin", "skel_warrior", "skel_rogue", "skel_mage" };
    bool have = false;
    for (const char* h : HAVE) have = have || !strcmp(h, lm.name);
    if (!have || g_nhulls >= MAX_HULLS || r3d::cel().outline_px <= 0.0f) return;
    char path[48];
    snprintf(path, sizeof path, "/rd/models/hull/%s.dmf", lm.name);
    eng::boot_log(path);
    if (dmf::load(path, &g_hull_m[g_nhulls], false) && r3d::gfx_init(&g_hull_g[g_nhulls], g_hull_m[g_nhulls], false) &&
        g_hull_m[g_nhulls].h->num_bones == lm.m.h->num_bones) {
        lm.gfx.hull = &g_hull_g[g_nhulls];
        ++g_nhulls;
    }
}

int models_get(const char* name, bool upload_texture) {
    for (int i = 0; i < g_nmodels; ++i)
        if (strcmp(g_models[i].name, name) == 0) return g_models[i].ok ? i : -1;
    if (g_nmodels >= MAX_MODELS) return -1;
    LoadedModel& lm = g_models[g_nmodels];
    snprintf(lm.name, sizeof lm.name, "%s", name);
    char path[48];
    snprintf(path, sizeof path, "/rd/models/%s.dmf", name);
    eng::boot_log(path);
    lm.ok = dmf::load(path, &lm.m, upload_texture) && r3d::gfx_init(&lm.gfx, lm.m, false);
    if (lm.ok && lm.m.h->num_bones) load_hull(lm);
    ++g_nmodels;
    return lm.ok ? g_nmodels - 1 : -1;
}

LoadedModel& models_at(int i) { return g_models[i]; }
int models_count() { return g_nmodels; }

// Translucent copy of a model's materials (TR list), for houses/trees that hide the hero.
void models_make_faded(LoadedModel& lm) {
    if (lm.faded.m) return;
    lm.faded = lm.gfx;
    for (int k = 0; k < lm.m.num_mats; ++k) {
        int f = eng::MAT_TRANSLUCENT;
        if (lm.m.mat_flags[k] & dmf::MAT_BILINEAR) f |= eng::MAT_BILINEAR;
        // front faces only while faded: the back walls/roof slope would stack more translucent
        // layers into the view (the house sits between the camera and the hero, i.e. close).
        lm.faded.single[k] = lm.faded.doubled[k] = eng::material_create(lm.m.mat_tex[k], f | eng::MAT_CULL);
    }
}

bool models_glow_range(const dmf::Model& m, int* first, int* count) {
    *first = *count = 0;
    for (int b = 0; b < m.h->num_batches; ++b) {
        const int mi = m.batches[b].flags >> 8;
        if (mi <= 0 || mi >= m.num_mats || m.mat_tex[mi] != 0) continue;
        int lo = 1 << 30, hi = -1;
        const uint16_t* w = m.strips + m.batches[b].first_word;
        const uint16_t* end = w + m.batches[b].num_words;
        while (w < end) { const int len = *w++; for (int j = 0; j < len; ++j) { lo = w[j] < lo ? w[j] : lo; hi = w[j] > hi ? w[j] : hi; } w += len; }
        if (hi >= lo) { *first = lo; *count = hi - lo + 1; return true; }
    }
    return false;
}

}
