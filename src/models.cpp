// models.cpp - see models.h (moved out of field3d.cpp for slice 3D-4; behaviour unchanged)
#include "models.h"
#include <stdio.h>
#include <string.h>

namespace bt {

static LoadedModel g_models[MAX_MODELS];
static int g_nmodels = 0;

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

}
