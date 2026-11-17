// models.h - the session's model registry, shared by the field (field3d) and the battle
// (battle3d): every romdisk/models/<name>.dmf is loaded once, with its PVR materials, and both
// modes draw the same instances (no second copy in RAM, no second upload to VRAM).
#pragma once
#include "dmf.h"
#include "render3d.h"

namespace bt {

struct LoadedModel {
    char          name[24];        // e.g. "house_plaster_r180" (18 chars)
    dmf::Model    m;
    r3d::ModelGfx gfx;             // opaque / as authored
    r3d::ModelGfx faded;           // TR copy (houses and trees that hide the hero), made on demand
    bool          ok;
};

const int MAX_MODELS = 40;

// Index of the loaded model (loads /rd/models/<name>.dmf on first use), -1 if missing or invalid.
// upload_texture = false: the embedded texture is not uploaded (weapons: drawn with the holder's).
int          models_get(const char* name, bool upload_texture = true);
LoadedModel& models_at(int i);
int          models_count();
void         models_make_faded(LoadedModel& lm);   // builds lm.faded once
// The vertex range of a character's untextured batch (the skeletons' glowing eyes: VS1 draws them unlit, in their
// vertex colour). false (count 0) if the model has none. The eye vertices are contiguous (tools/vs1art).
bool         models_glow_range(const dmf::Model& m, int* first, int* count);

}
