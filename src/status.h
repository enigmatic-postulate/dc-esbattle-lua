// status.h - port of battle/status.lua (status definitions + turn hooks).
#pragma once
#include "battle_state.h"
namespace bt {

typedef void (*StatusHook)(BattleCtx&, Actor&, StatusInst&);

struct StatusDef {
    const char* key;            // "burn" (trace/debug name)
    const char* name;           // "Burn"
    StatusHook  on_apply;       // any hook may be null
    StatusHook  on_turn_start;
    StatusHook  on_turn_end;
    StatusHook  on_expire;
    bool        skip_turn;
};

const StatusDef& status_def(StatusId id);

bool        status_has(const Actor& a, StatusId id);
StatusInst* status_get(Actor& a, StatusId id);
void        status_add(BattleCtx& ctx, Actor& a, StatusId id, int turns, int dot = 2);
void        status_remove(Actor& a, StatusId id);
void        status_tick_turns(BattleCtx& ctx, Actor& a);

struct TurnStart { bool can_act; bool skipped; };
TurnStart   status_on_turn_start(BattleCtx& ctx, Actor& a);
void        status_on_turn_end(BattleCtx& ctx, Actor& a);

}
