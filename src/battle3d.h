// battle3d.h - the 3D battle presentation (slice 3D-4).
//
// Read-only on the rules (BattleCtx), like field3d is on the field: it consumes the presentation
// events the state machine pushes (an action was resolved, a burn ticked) and plays a sequence for
// each - the attacker runs in and swings, the blow lands on the clip's impact frame, numbers and HP
// bars follow at impact, fireballs fly, skeletons collapse. While a sequence plays,
// battle3d_busy() is true and the state machine waits (battle.cpp gates); that changes when the
// next state comes, never what happens (tests/battle3d: turn-level equivalence).
//
// Everything that can delay the rules is counted in whole ticks (clip lengths and events are
// integer ticks in the .dma), so the float and double builds and the SH4 build gate identically.
// Cosmetics that may vary (particles, shake) use their own RNG, never eng::random01().
#pragma once
#include "battle_state.h"
#include "encounters.h"

namespace bt {

void battle3d_init();                                    // once (models, clips, FX, arenas)
void battle3d_begin(BattleCtx& ctx, const EncounterDef& enc);
void battle3d_tick(BattleCtx& ctx);                      // once per battle tick, before the state machine
bool battle3d_busy();                                    // a sequence (or the intro) is playing
void battle3d_draw(BattleCtx& ctx);

// Host/tooling: set a named tuning value (see TUNES in battle3d.cpp). false if unknown.
bool battle3d_tune(const char* name, float value);

struct Battle3dInfo {
    bool  ready;              // models + library loaded
    int   seq_kind, seq_t, queued;
    bool  busy, intro, outro;
    int   particles, projectiles;
    float cam_eye[3], cam_target[3], cam_fov;
    int   shown_hp[MAX_ACTORS];
    // last frame's submitted vertices per part (draw cost breakdown)
    int   cost_chars, cost_weapons, cost_ground, cost_props, cost_fx;
    // last frame: screen x of each living enemy, in actor order (target-cursor order checks)
    int   n_enemies;
    float enemy_sx[MAX_ACTORS];
};
Battle3dInfo battle3d_info();
const char* battle3d_seq_name(int kind);

}
