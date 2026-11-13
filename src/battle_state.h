// battle_state.h - typed replacements for the Lua tables used by the battle scripts.
//
// Lua -> C++ mapping
//   actor table {id,team,hp,hp_max,stats,status,x,y,w,h,spr,_hurt_t} -> Actor (spr -> sheet+facing, _hurt_t -> hurt_ticks)
//   actor.status = { {id=..,turns=..,dot=..}, ... }                   -> FixedVec<StatusInst>
//   target lists (arrays of actor *references*)                         -> ActorList (Actor*)
//   ctx = {state, actors, order, turn_i, active, pending, anim_t}      -> BattleCtx
//   module-local state in presentation.lua / fx.lua / ui.lua            -> Presentation / Fx / Ui
//   string-keyed ids ("burn", "enemy_all", "player", "command")          -> enums
#pragma once
#include "common.h"
#include "engine.h"
#include "chars.h"

namespace bt {

const int MAX_ACTORS  = 8;
const int MAX_STATUS  = 8;   // per actor; Lua was unbounded (burns stack). Overflow drops the new entry.
const int MAX_MOVES   = 32;  // presentation.lua `moves`
const int MAX_FLASHES = 32;  // presentation.lua `flashes`
const int MAX_FLOATS  = 64;  // fx.lua `floats`

enum class Team : uint8_t { Player, Enemy };

enum class StatusId : uint8_t { Burn, Guard, Stun, Count };

struct StatusInst {
    StatusId id;
    int      turns;
    int      dot;    // burn damage per tick (Lua: st.dot or 2)
};

struct Stats {
    int str, def, mdef, intel, agi;   // Lua fell back mdef -> def and int -> 0; data is explicit here
};

struct Actor {
    const char* id;
    Team        team;
    int         hp, hp_max;
    Stats       stats;
    FixedVec<StatusInst, MAX_STATUS> status;
    real        x, y, w, h;
    CharSheet   sheet;        // battle look (was a sprite handle)
    Dir         facing;
    int         hurt_ticks;   // Lua: actor._hurt_t; marker drawn while > 0
};

typedef FixedVec<Actor*, MAX_ACTORS> ActorList;

enum class TargetMode : uint8_t { None, Self, EnemySingle, EnemyAll, AllyAll };

// ---- skills -------------------------------------------------------------------
struct Hit { Actor* target; int dmg; };

enum class ResultKind : uint8_t { Fail, Single, Aoe, Self, Noop };

struct SkillResult {
    bool       ok     = false;
    ResultKind kind   = ResultKind::Fail;
    Actor*     target = nullptr;
    int        dmg    = 0;
    FixedVec<Hit, MAX_ACTORS> hits;
};

struct BattleCtx;
struct SkillDef {
    const char* id;
    const char* name;
    TargetMode  target_mode;
    SkillResult (*resolve)(BattleCtx&, Actor& user, const ActorList& targets);                  // may be null
    void        (*present)(BattleCtx&, Actor& user, const SkillResult&, const ActorList& targets); // may be null
};

struct PendingAction {
    const SkillDef* skill = nullptr;   // nullptr == Lua `ctx.pending = nil`
    ActorList       targets;
    int             cursor_index = 0;  // 0-based (Lua: 1-based)
};

// ---- presentation.lua module state -------------------------------------------
struct PresMove  { Actor* actor; int t, dur; real nx, ny, dist; };   // t, dur in ticks
struct PresFlash { Actor* actor; int t, dur; };
struct Presentation {
    FixedVec<PresMove, MAX_MOVES>    moves;
    FixedVec<PresFlash, MAX_FLASHES> flashes;
};

// ---- fx.lua module state -----------------------------------------------------
struct FloatText {
    real     x, y, vx, vy;
    int      t, dur;        // ticks
    char     text[12];
    uint32_t color;
    real     scale;
};
struct Fx {
    FixedVec<FloatText, MAX_FLOATS> floats;
};

// ---- ui.lua + input_probe.lua module state ----------------------------------
enum UiButton { UB_FIRE, UB_LEFT, UB_RIGHT, UB_B, UB_Y, UB_COUNT };
// Home: command ring. Target: pick one enemy (Attack). TargetAll: confirm an all-enemy skill (Fire).
enum class UiState : uint8_t { Home, Target, TargetAll };
struct Ui {
    int     bob_ticks = 0;
    UiState state = UiState::Home;
    int     home_i = 0;              // 0=Attack 1=Fireball 2=Guard (Lua 1..3)
    int     target_i = 0;            // 0-based (Lua 1-based)
    int     target_entry_lock = 0;   // ticks
};

// ---- battle.lua ctx ----------------------------------------------------------
enum class BattleState : uint8_t { Command, Resolve, Animate, TurnAdvance, Win, Lose };
enum class BattleOutcome : uint8_t { None, Won, Lost };

struct BattleCtx {
    BattleState state = BattleState::Command;
    FixedVec<Actor, MAX_ACTORS> actors;   // storage; Actor* into here are stable
    ActorList     order;
    int           turn_i = 0;             // 0-based index into order
    Actor*        active = nullptr;
    PendingAction pending;
    int           anim_ticks = 0;
    int           result_ticks = 0;            // ticks spent in Win/Lose (banner)
    BattleOutcome outcome = BattleOutcome::None; // set once the banner is confirmed
    int           idle_ticks = 0;              // free-running (idle animation)

    Presentation pres;
    Fx           fx;
    Ui           ui;

    BattleCtx() {}
    BattleCtx(const BattleCtx&) = delete;             // Actor* must stay valid
    BattleCtx& operator=(const BattleCtx&) = delete;
};

inline bool alive(const Actor* a) { return a && a->hp > 0; }

} // namespace bt
