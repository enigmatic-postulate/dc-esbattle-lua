// field3d.h - 3D presentation of the field (3D slice 1): the tile map as a ground plane, the
// hero as the KayKit Knight (.dmf, skinned, idle/walk), placeholders for the other entities
// (slime -> cube, enemy/NPC -> sphere), blob shadows and a follow camera.
//
// Read-only view of the field logic (field.h): it never changes game state. Its own state is
// cosmetic (camera smoothing, hero yaw, animation clocks) and advances once per tick in
// field3d_tick(), so drawing has no side effects and the logic traces are unaffected.
#pragma once

namespace bt {

// World mapping: tile (tx, ty) covers x in [tx, tx+1], z in [ty, ty+1]; 1 tile = 1 unit;
// map "down" (+y) is +Z, towards the camera.
void field3d_init();      // loads models/materials once (after field_load)
void field3d_reset();     // snap camera + hero facing to the current logic state
void field3d_tick();      // cosmetic update, once per game tick while the field is shown
void field3d_draw(int hero_hp, int hero_hp_max);

// Host/tooling hook: set a named view parameter (see TUNES in field3d.cpp). false if unknown.
bool field3d_tune(const char* name, float value);

struct Field3dInfo { bool knight_ok, cube_ok, sphere_ok; float cam_x, cam_z, hero_yaw, walk_weight; };
Field3dInfo field3d_info();   // for traces / tests

}
