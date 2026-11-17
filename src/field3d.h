// field3d.h - 3D presentation of the field (3D slice 1): the tile map as a ground plane, the
// hero as the KayKit Knight (.dmf, skinned, idle/walk), the map's entities (skeletons; since slice 3D-5
// Wren and Brom on the shared animation library), blob shadows and a follow camera.
//
// Read-only view of the field logic (field.h): it never changes game state. Its own state is
// cosmetic (camera smoothing, hero yaw, animation clocks) and advances once per tick in
// field3d_tick(), so drawing has no side effects and the logic traces are unaffected.
//
// Slice 3D-5 adds: the dialog camera (a two-shot eased in over T3 and out over T4, with a fallback to the
// field camera when its eye would sit inside a prop), NPCs turning to the hero, gestures (`anim`),
// markers (! / ...) and emotes as 3D billboards, the 'A Talk' prompt and the objective HUD.
#pragma once

namespace bt {

// World mapping: tile (tx, ty) covers x in [tx, tx+1], z in [ty, ty+1]; 1 tile = 1 unit;
// map "down" (+y) is +Z, towards the camera.
void field3d_init();      // loads models/materials once (after field_load)
void field3d_reset();     // snap camera + hero facing to the current logic state
void field3d_tick();      // cosmetic update, once per game tick while the field is shown
void field3d_draw();      // the 3D scene, billboards and the talk prompt
// The field HUD: HP and, under it, the inventory (top right: potions, Wren's token once given) and the
// objective plate (top left, dialog font; the text with {patrols} expanded). show = false hides it all
// (dialog, transitions).
void field3d_draw_hud(int hero_hp, int hero_hp_max, int potions, bool token, const char* objective, bool show);

// ---- slice 3D-5
// Marker over entity i: -1 none, 0 quest '!', 1 optional talk '...' (re-evaluated by game.cpp).
void field3d_set_marker(int entity, int kind);
void field3d_dialog_begin(int npc);       // T3: the two-shot for this NPC, its greeting (interact)
void field3d_play(int actor, int clip);   // `anim`: actor 0 = Rowan, 1 + i = entity i; library clip
void field3d_emote(int actor, int kind);  // `emote`: 60 ticks over the actor's head
void field3d_show_prompt(bool on);        // the 'A Talk' prompt over field_talk_target() (Field mode only)
// The two-shot the dialog camera eases to, for a hero at (hx, hz) and an NPC at (nx, nz) (tiles):
// returns false (and the field camera's eye) when the eye would be inside a prop's box (+0.3).
bool field3d_two_shot(float hx, float hz, float nx, float nz, float eye[3], float look[3]);

// Host/tooling hook: set a named view parameter (see TUNES in field3d.cpp). false if unknown.
bool field3d_tune(const char* name, float value);

struct Field3dInfo {
    bool knight_ok, cube_ok, sphere_ok; float cam_x, cam_z, hero_yaw, walk_weight;
    bool npc_ok;                 // Wren's and Brom's models and the library binding
    bool dlg_cam_ok;             // the current two-shot is valid (false: the field camera is used)
    float eye[3], look[3];       // the camera this tick
    int  markers, emotes;        // billboards drawn last frame
    int  prompt;                 // the NPC the 'A Talk' prompt is over (-1: none)
};
Field3dInfo field3d_info();   // for traces / tests

}
