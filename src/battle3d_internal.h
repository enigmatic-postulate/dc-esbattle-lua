// battle3d_internal.h - state shared by battle3d.cpp (tick side) and battle3d_draw.cpp (drawing).
#pragma once
#include "anim.h"
#include "arena.h"
#include "battle3d.h"
#include "render3d.h"

namespace bt {

struct Tune3 { const char* name; float* value; };

// -- looks
enum Kind { K_HERO, K_WARRIOR, K_ROGUE, K_MAGE, K_WARDEN, K_MINION, K_COUNT };
struct KindDef {
    const char* display;             // HUD name (5x7 font: upper case)
    const char* model;
    float       scale;               // x the common character scale
    const char* wpn_r;               // weapon models (right / left hand slot), nullptr = none
    const char* wpn_l;
    const char* idle, *hit, *death, *atk0, *atk1;   // library clip names
    bool        ranged;              // attacks from its slot (the Mage's bolt)
    float       height;              // model units to the top of the head (HUD anchors)
};
extern const KindDef KINDS[K_COUNT];
struct KindRuntime { int model; bool ok; anim::Binding bind; int wpn[2]; };

// -- actor views
struct View {
    Actor*  a;
    int     kind;
    bool    present, asleep, dying, gone;
    anim::Animator an;
    int     c_idle, c_hit, c_death, c_atk[2], atk_alt, collapse_t;
    r3d::Vec3 home, pos, hand;
    float   home_yaw, yaw, yaw_goal, scale, alpha;
    r3d::Vec3 mv_from, mv_to;
    int     mv_t, mv_dur;
    int     shown_hp, bar_delay;
    float   bar;
    int     flash, dead_t, charge, charge_kind;
    bool    guard_shown, burn_shown;
    bool    charge_shown;        // the Warden's CHARGE: wind-up held, dark motes gather (until Soul Rend)
    int     rise_t;              // > 0: rising from the ground (Raise Dead), ticks left
    int     lit_bucket;
};

// -- camera
enum Shot { SHOT_OVERVIEW, SHOT_LEAN, SHOT_PULLBACK, SHOT_ACTION, SHOT_FOCUS, SHOT_CAST, SHOT_INTRO, SHOT_VICTORY, SHOT_DEFEAT };
struct CamShot { r3d::Vec3 eye, target; float fov; };
struct CamRig { CamShot cur; int shot, u, t; float rate, shake; bool cut; };

// -- sequences
enum SeqKind { SEQ_NONE, SEQ_INTRO, SEQ_MELEE, SEQ_BOLT, SEQ_FIREBALL, SEQ_GUARD, SEQ_BURN,
               SEQ_POTION, SEQ_CHARGE, SEQ_SOUL_REND, SEQ_GRAVE_FIRE, SEQ_WARD, SEQ_SUMMON, SEQ_PHASE, SEQ_CRUMBLE };
enum Op : uint8_t { OP_PLAY, OP_IDLE, OP_MOVE, OP_FACE, OP_FACE_POINT, OP_FACE_HOME, OP_CAM, OP_IMPACT, OP_LAUNCH,
                    OP_CHARGE, OP_GUARD_ON, OP_BURST, OP_BURN_HIT, OP_END,
                    OP_HOLD_AT,       // a: ticks into the clip to freeze at
                    OP_CHARGE_ON,     // the Warden starts charging (held pose, motes, caption)
                    OP_WAVE,          // a dark wave runs along the ground from actor to hit 0's target (a = ticks)
                    OP_ERUPT,         // purple fire under hit k's target (a = k)
                    OP_RING_PULSE,    // an expanding rune ring under the actor (b = colour index)
                    OP_RAISE,         // every hit of the event rises from the ground (Raise Dead / phase)
                    OP_CRUMBLE,       // hits with dmg = 1 (phase) or every hit (Crumble event) collapse
                    OP_PHASE2,        // the boss's rune ring turns red
                    OP_CAPTION,       // a = caption id
                    OP_HEAL,          // the potion: the user's shown HP rises by -ev.dmg
                    OP_NAMECARD };    // the boss's name card
struct Cue { int16_t t; uint8_t op; int8_t actor; int16_t a, b; float f; r3d::Vec3 p; };
const int MAX_CUES = 40;
struct Seq {
    int  kind;
    bool started, ended;
    int  t, next;
    BattleEvent ev;
    FixedVec<Cue, MAX_CUES> cues;
};

// -- effects
enum FxCell { FX_RUNE, FX_RING, FX_GLOW, FX_STAR, FX_FLAME, FX_SMOKE, FX_ARROW, FX_BUBBLE, FX_SHARD, FX_DOT, FX_CELL_COUNT };
enum Burst { BURST_SPARKS, BURST_FIRE, BURST_ARCANE, BURST_DUST, BURST_GUARD, BURST_DARK, BURST_HEAL, BURST_WISP };
enum Caption { CAP_NONE, CAP_CHARGE, CAP_PHASE };
enum ProjKind { PROJ_FIREBALL, PROJ_BOLT, PROJ_WAVE };
struct Particle { r3d::Vec3 p, v; int age, life; float size0, size1, rot, spin; uint8_t cell; bool additive; uint32_t c0, c1; float grav; };
struct Projectile { r3d::Vec3 from, to, pos; int t, dur, kind, hit; float arc; };
struct FloatText3 { r3d::Vec3 p; int age, life; char text[12]; uint32_t color; int scale; };

const int MAX_PARTICLES = 192;
const int MAX_SEQ = 12;

struct B3 {
    bool inited, lib_ok, arena_ok;
    anim::Library lib;
    KindRuntime kinds[K_COUNT];
    Arena arenas[2];
    const Arena* arena;
    View views[MAX_ACTORS];
    int  num_views;
    FixedVec<Seq, MAX_SEQ> queue;
    FixedVec<Particle, MAX_PARTICLES> parts;
    FixedVec<Projectile, 8> projs;
    FixedVec<FloatText3, 24> texts;
    CamRig cam;
    int  hitstop, frame, outro, outro_t, skip_hold;
    // the boss fight
    int  boss_vi;              // view of the boss (-1: none)
    bool boss_phase2;          // the rune ring under him is red
    int  caption, caption_t;   // caption id and ticks left (0: none)
    int  namecard_t;           // ticks left of the big name card
    int  ring_pulse_t;         // expanding ring under the boss (Raise Dead, phase)
    uint32_t rng;
    BattleCtx* ctx;
    int  cost[5];          // last frame: vertices out for characters, weapons, ground, props, fx
    int  n_enemy_sx;       // last frame: screen x of the living enemies (actor order)
    float enemy_sx[MAX_ACTORS];
};
extern B3 g_b3;

// tuning (battle3d.cpp)
extern float T_CHAR_SCALE, T_SHADOW_ALPHA;

// battle3d.cpp helpers used by the drawing side
View* view_of(const Actor* a);
r3d::Vec3 view_chest(const View& v);
r3d::Vec3 view_head(const View& v);
float ease(float u);
void cam_camera(r3d::Camera* out);
// battle3d_draw.cpp
void b3_draw_init();
void b3_prebake();     // bakes every view's lighting (battle start)

}
