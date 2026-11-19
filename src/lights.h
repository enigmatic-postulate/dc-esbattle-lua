// lights.h - light presets (slice 3D-6, docs/NEXT_SLICE_BARROW.md 5.6, 7.4): romdisk/maps/lights.txt, one line per
// preset, read once at boot:
//
//   light <name> dir <x> <y> <z> ambient <a> diffuse <d> colour <rrggbb> tint <rrggbb> clear <rrggbb|ground>
//         [fog <rrggbb> <start> <end>]
//
// dir      towards the light (normalised at load); ambient + diffuse * max(0, n.dir) lights the characters and the
//          runtime-lit props (the kit's trees and fences, the barrow's tombs);
// colour   multiplies that lighting (the light's colour);
// tint     multiplies what is baked (the VS1 village props), the painted ground and the billboards - PRELIT models
//          take the tint only (they are never relit);
// clear    the background colour ("ground": the map's outer ground colour x tint, as the day look has always done);
// fog      PVR table fog: colour and the linear range in view depth (world units).
//
// A map names its preset (`light night`); scripts switch (`light dawn`) or blend (`light lerp night dawn 240`).
// `day` reproduces the look of every slice before this one exactly (white colour and tint, no fog).
#pragma once
#include <stdint.h>

namespace bt {

struct LightPreset {
    char     name[12];
    float    dir[3];
    float    ambient, diffuse;
    uint32_t colour, tint;            // 0xRRGGBB
    bool     clear_ground;            // clear = the ground's outer colour x tint
    uint32_t clear;
    bool     fog;
    uint32_t fog_rgb;
    float    fog_start, fog_end;
};

const int MAX_LIGHTS = 8;

// Loads /rd/maps/lights.txt (false + a logged line number on an error). Without the file there is one preset, `day`.
bool lights_load();
int  lights_count();
int  light_find(const char* name);                // -1 if absent
const LightPreset& light_get(int i);             // i out of range: `day`
// a + (b - a) * t, t in [0, 1]; fog blends through "no fog" as fog pushed far away (60..90 units).
void light_mix(const LightPreset& a, const LightPreset& b, float t, LightPreset* out);
uint32_t rgb_mul(uint32_t argb, uint32_t rgb);   // per channel (alpha kept): argb x rgb / 255

}
