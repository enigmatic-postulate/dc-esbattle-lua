// field.h - tile-based stage: map loading, free analog hero movement with tile collision,
// entities (tile-stepping, optional patrol), touch -> encounter.
// Slice tasks B1-B4. Everything is tick-based (60/s) and deterministic (integer math only).
// Drawing lives in field3d.cpp (3D slice): the map is a logic grid that the 3D view lays out
// as a ground plane, 1 tile = 1 world unit.
#pragma once
#include "common.h"
#include "chars.h"

namespace bt {

const int TILE_PX         = 32;   // 16x16 art drawn at 2x
const int MAX_MAP_W       = 64, MAX_MAP_H = 64;
const int MAX_ENTITIES    = 16;
const int MAX_PROPS       = 128;
const int SUBPX           = 16;   // hero positions are integers in 1/16 px (deterministic, no floats)
// Hero speed at full tilt, in 1/256 of a sub-pixel per tick. 24 subpx/tick = 1.5 px/tick
// = 2.81 tiles/s (VS1: 14 = 1.64 tiles/s; the 2D build: 32 = 3.75 tiles/s). The KayKit run clip
// covers 1.55 tiles per cycle at the Knight's 0.8 scale, so full speed plays it at ~1.45x; low
// stick tilt walks (field3d.cpp blends walk -> run by speed).
const int HERO_SPEED_Q8 = 24 * 256;
// Analog stick (-128..127): radial dead zone, then a linear ramp to full speed at STICK_FULL.
// The d-pad is treated as a full-tilt stick (8 directions, same speed on diagonals).
const int STICK_DEAD = 24;
const int STICK_FULL = 100;
// Hero "feet" hitbox and entity body box, in px relative to the 32x32 sprite cell's top-left.
const int HERO_BOX_X0 = 8, HERO_BOX_X1 = 24, HERO_BOX_Y0 = 18, HERO_BOX_Y1 = 30;
const int ENT_BOX_X0  = 4, ENT_BOX_X1  = 28, ENT_BOX_Y0  = 10, ENT_BOX_Y1  = 30;
const int NPC_STEP_TICKS  = 60;   // 1 tile/s: the skeletons' shuffle (VS1: 16 = 3.75 tiles/s, 5x their walk clip)
const int PATROL_WAIT     = 45;   // ticks between patrol steps

// Order MUST match TILES in tools/make_assets.py.
enum Tile : uint8_t { TILE_GRASS, TILE_FLOWERS, TILE_PATH, TILE_WALL, TILE_TREE, TILE_WATER, TILE_WATER2, TILE_COUNT };
enum class EntityKind : uint8_t { Enemy, Npc };
// 3D look of a map entity (the `look` column): slime -> Skeleton Warrior, guard -> Skeleton Mage (the
// Bone Warden), mage -> Wren (the human Mage), smith -> Brom (the Barbarian). Slice 3D-5 adds the last two.
enum EntityLook : uint8_t { ELOOK_SLIME, ELOOK_GUARD, ELOOK_MAGE, ELOOK_SMITH, ELOOK_COUNT };

// Talk rules (slice 3D-5, docs/NEXT_SLICE_DIALOG.md 5.1; GDD 7.2), all integer:
// range 1.3 tiles from the hero's feet-box centre to the NPC's tile centre, in 1/16 px:
// d^2 <= (1.3 x 512)^2; angle within 60 degrees of the facing vector: dot > 0 and 4 dot^2 >= |f|^2 |d|^2.
const long long TALK_RANGE2 = 443023;
const int NPC_NOTICE_TILES = 3;   // NPCs pause their patrol and face the hero within 3 tiles (Chebyshev)

struct Hero {
    int  x, y;        // sprite cell top-left, in 1/16 px (world space)
    Dir  dir;         // 4-way facing (kept for traces/battle); the 3D view faces (vx, vy)
    bool moving;      // moved this tick
    int  walk_sub;    // distance walked (1/16 px, Manhattan)
    int  vx, vy;      // requested velocity this tick, 1/256 subpx (0 when no input)
    int  fx, fy;      // fractional remainders of the motion, 1/256 subpx
    int  face_x, face_y;   // facing vector (slice 3D-5): the last non-zero requested velocity; the talk test's 60 degrees
};

struct Walker {       // tile-stepping walker (entities)
    int  tx, ty;      // current tile (origin while a step is in progress)
    int  nx, ny;      // destination tile while moving
    bool moving;
    int  step;        // ticks into the current step (0..len)
    int  step_len;
    Dir  dir;
    bool phase;       // alternates walk frame A/B each step
};

struct Entity {
    char       id[16];
    EntityKind kind;
    CharSheet  sheet;
    Walker     w;
    char       encounter[24];
    bool       alive;
    bool       patrol;              // back and forth along (pdx,pdy) for plen tiles
    int        pdx, pdy, plen, ppos, psign, pwait;
    int        home_x, home_y;
    Dir        home_dir;
    char       after[2][16];        // `after a [b]`: dormant (not drawn, not touchable) until these are defeated
    int        n_after;
    bool       dormant;
    // slice 3D-5
    EntityLook look;
    char       talk[16];            // npc: `talk <script>` (resolved by script_load)
    char       win[16];             // enemy: `win <flag>`, set when its battle is won ("" = none)
    char       cond[64];            // `if <cond...>`: the raw condition (compiled by script_load; "" = always)
    bool       hidden;              // `if` false: not drawn, not touchable, not blocking, frozen
    bool       facing_hero;         // an NPC holding its patrol to face the hero (within NPC_NOTICE_TILES)
};

// A placed 3D model from a map `prop` line. The logic only uses the footprint (solid tiles);
// the model name/position/rotation are for the 3D view.
struct Prop {
    char  model[16];
    float x, y;           // model origin, in tiles (world x, z)
    float rot_deg;        // yaw; 0 = the model's front faces +y (south, towards the camera)
    bool  solid;
    int   tx, ty, tw, th; // footprint in tiles (solid props only)
};

struct FieldState {
    char    name[32];
    int     w, h;
    uint8_t tiles[MAX_MAP_W * MAX_MAP_H];
    uint8_t solid[MAX_MAP_W * MAX_MAP_H];  // 1 = covered by a solid prop (blocks like a wall)
    FixedVec<Prop, MAX_PROPS> props;
    int     spawn_x, spawn_y;
    Dir     spawn_dir;
    Hero    hero;
    FixedVec<Entity, MAX_ENTITIES> ents;
    int     cam_x, cam_y;      // legacy 2D camera window (px); kept for traces / invariant F4
    int     ticks;
    int     contact;      // entity index touched this tick (-1 = none); consumed by field_take_contact()
    int     talk_target;  // NPC the 'A Talk' prompt points at this tick (-1 = none); slice 3D-5
};

bool field_load(const char* path);          // parse a map file; false on error (reported via dbg_print)
void field_reset();                         // hero at spawn, every entity alive at home
void field_update();                        // one tick; reads input_held() / input_stick()
int  field_take_contact();                  // touched entity index (once), or -1
void field_entity_pixel(int i, int* px, int* py);   // entity cell top-left in px (interpolated step)
void field_defeat(int entity_index);        // removes the entity for this session
const FieldState& field_state();
// ---- slice 3D-5 (talk, quest)
int  field_talk_target();                   // the NPC that A would talk to now (-1: none); field_update computes it
void field_set_hidden(int entity_index, bool hidden);   // `if` conditions (game.cpp re-evaluates them)
// Checkpoint restore: hero on tile (tx, ty) facing dir; entity i is gone if bit i of `defeated` is set,
// dormant ones wake as after field_defeat; everyone else at home.
void field_restore(int tx, int ty, Dir dir, uint32_t defeated);
void field_hero_tile(int* tx, int* ty);     // tile under the hero's feet-box centre
void field_hero_face(int dx, int dy);       // facing vector (any scale) + the 4-way dir; the hero turns to an NPC
void field_hero_face_entity(int entity_index);
void field_entity_face(int entity_index, Dir d);
void field_entity_face_hero(int entity_index);   // 4-way, toward the hero
Dir  dir_toward(int dx, int dy);            // 4-way: the dominant axis (|dx| > |dy|: horizontal)
EntityLook look_by_name(const char* name, bool* ok);
// Host test hooks (tests/dialog: the prompt-geometry probe): place the hero (sprite cell top-left, 1/16 px)
// with a facing vector, and recompute the talk target as field_update would.
void field_debug_place_hero(int x, int y, int face_x, int face_y);
int  field_debug_talk_target();
bool tile_walkable(uint8_t t);              // by tile type only
bool cell_walkable(int x, int y);           // in bounds, walkable tile type, no solid prop
const char* dir_name(Dir d);

} // namespace bt
