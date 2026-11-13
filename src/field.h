// field.h - tile-based stage: map loading, free 8-direction hero movement with tile collision,
// entities (tile-stepping, optional patrol), touch -> encounter.
// Slice tasks B1-B4. Everything is tick-based (60/s) and deterministic.
#pragma once
#include "common.h"
#include "chars.h"

namespace bt {

const int TILE_PX         = 32;   // 16x16 art drawn at 2x
const int MAX_MAP_W       = 64, MAX_MAP_H = 64;
const int MAX_ENTITIES    = 16;
const int SUBPX           = 16;   // hero positions are integers in 1/16 px (deterministic, no floats)
const int HERO_SPEED      = 32;   // 2 px/tick (= 1 tile per 16 ticks) when moving straight
const int HERO_SPEED_DIAG = 23;   // per axis on diagonals: 23*sqrt(2) = 32.5 ~ same overall speed
// Hero "feet" hitbox and entity body box, in px relative to the 32x32 sprite cell's top-left.
const int HERO_BOX_X0 = 8, HERO_BOX_X1 = 24, HERO_BOX_Y0 = 18, HERO_BOX_Y1 = 30;
const int ENT_BOX_X0  = 4, ENT_BOX_X1  = 28, ENT_BOX_Y0  = 10, ENT_BOX_Y1  = 30;
const int NPC_STEP_TICKS  = 16;
const int PATROL_WAIT     = 45;   // ticks between patrol steps

// Order MUST match TILES in tools/make_assets.py.
enum Tile : uint8_t { TILE_GRASS, TILE_FLOWERS, TILE_PATH, TILE_WALL, TILE_TREE, TILE_WATER, TILE_WATER2, TILE_COUNT };
enum class EntityKind : uint8_t { Enemy, Npc };

struct Hero {
    int  x, y;        // sprite cell top-left, in 1/16 px (world space)
    Dir  dir;
    bool moving;      // moved this tick
    int  walk_sub;    // distance walked (1/16 px), drives the walk cycle
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
};

struct FieldState {
    char    name[32];
    int     w, h;
    uint8_t tiles[MAX_MAP_W * MAX_MAP_H];
    int     spawn_x, spawn_y;
    Dir     spawn_dir;
    Hero    hero;
    FixedVec<Entity, MAX_ENTITIES> ents;
    int     cam_x, cam_y;
    int     ticks;
    int     contact;      // entity index touched this tick (-1 = none); consumed by field_take_contact()
};

bool field_load(const char* path);          // parse a map file; false on error (reported via dbg_print)
void field_reset();                         // hero at spawn, every entity alive at home
void field_update();                        // one tick; reads input_held()
void field_draw(int hero_hp, int hero_hp_max);
int  field_take_contact();                  // touched entity index (once), or -1
void field_defeat(int entity_index);        // removes the entity for this session
const FieldState& field_state();
bool tile_walkable(uint8_t t);
const char* dir_name(Dir d);

} // namespace bt
