// status.cpp - port of battle/status.lua
#include "status.h"
#include "combat.h"
#include "fx.h"
#include "presentation.h"

namespace bt {

// ---- definitions (Lua: M.defs) ----------------------------------------------
static void burn_on_apply(BattleCtx& ctx, Actor& a, StatusInst&) { fx_float_status(ctx, a, "BURN"); }

static void burn_on_turn_start(BattleCtx& ctx, Actor& a, StatusInst& st) {
    if (a.hp <= 0) return;
    int dmg = st.dot;
    apply_damage(&a, dmg);
    BattleEvent ev;                                  // presentation only (slice 3D-4): the burn sequence
    ev.kind = EvKind::BurnTick; ev.user = &a; ev.skill = -1; ev.result = ResultKind::Noop; ev.dmg = dmg;
    ctx.events.push(ev);
    fx_float_damage(ctx, a, dmg, 0xFFFFA040);
    a.hurt_ticks = maxv(a.hurt_ticks, T_HURT_HIT);
}

static void guard_on_apply(BattleCtx& ctx, Actor& a, StatusInst&) { fx_float_status(ctx, a, "GUARD"); }
// Guard fix (rules().guard_fix): the guard drops when its owner's next turn starts.
static void guard_on_turn_start(BattleCtx&, Actor&, StatusInst& st) { if (rules().guard_fix) st.turns = 0; }
static void stun_on_apply(BattleCtx& ctx, Actor& a, StatusInst&)  { fx_float_status(ctx, a, "STUN"); }

static const StatusDef DEFS[(int)StatusId::Count] = {
    //  key      name     on_apply        on_turn_start       on_turn_end on_expire skip_turn
    { "burn",  "Burn",  burn_on_apply,  burn_on_turn_start, nullptr,    nullptr,  false },
    { "guard", "Guard", guard_on_apply, guard_on_turn_start, nullptr,   nullptr,  false },
    { "stun",  "Stun",  stun_on_apply,  nullptr,            nullptr,    nullptr,  true  },
    { "charge", "Charge", nullptr,      nullptr,            nullptr,    nullptr,  false },   // the Warden's telegraph
};

const StatusDef& status_def(StatusId id) { return DEFS[(int)id]; }

// ---- helpers -------------------------------------------------------------------
bool status_has(const Actor& a, StatusId id) {
    for (const StatusInst& s : a.status) if (s.id == id) return true;
    return false;
}

StatusInst* status_get(Actor& a, StatusId id) {
    for (StatusInst& s : a.status) if (s.id == id) return &s;
    return nullptr;
}

void status_add(BattleCtx& ctx, Actor& a, StatusId id, int turns, int dot) {
    if (!a.status.push(StatusInst{ id, turns, dot })) return;   // capacity guard (Lua: unbounded)
    StatusInst& st = a.status[a.status.size() - 1];
    const StatusDef& def = status_def(id);
    if (def.on_apply) def.on_apply(ctx, a, st);
}

void status_remove(Actor& a, StatusId id) {
    int i = 0;
    while (i < a.status.size()) {
        if (a.status[i].id == id) a.status.swap_remove(i);
        else ++i;
    }
}

void status_tick_turns(BattleCtx& ctx, Actor& a) {
    int i = 0;
    while (i < a.status.size()) {
        StatusInst& st = a.status[i];
        st.turns -= 1;
        if (st.turns <= 0) {
            const StatusDef& def = status_def(st.id);
            if (def.on_expire) def.on_expire(ctx, a, st);
            a.status.swap_remove(i);
        } else {
            ++i;
        }
    }
}

TurnStart status_on_turn_start(BattleCtx& ctx, Actor& a) {
    if (!alive(&a)) return TurnStart{ false, false };

    // Lua `for i = 1, #actor.status` evaluates the bound once; hooks don't resize.
    const int n = a.status.size();
    for (int i = 0; i < n; ++i) {
        const StatusDef& def = status_def(a.status[i].id);
        if (def.on_turn_start) def.on_turn_start(ctx, a, a.status[i]);
    }

    // Statuses a hook ended at this turn start (the guard fix) go now; none exist under the legacy
    // rules, where every status still has turns >= 1 here.
    for (int i = 0; i < a.status.size();) {
        if (a.status[i].turns <= 0) {
            const StatusDef& def = status_def(a.status[i].id);
            if (def.on_expire) def.on_expire(ctx, a, a.status[i]);
            a.status.swap_remove(i);
        } else {
            ++i;
        }
    }

    if (!alive(&a)) return TurnStart{ false, false };   // DOT killed the actor

    for (const StatusInst& st : a.status) {
        if (status_def(st.id).skip_turn) {
            fx_float_text_xy(ctx, a.x, a.y - a.h * (real)1.15, "SKIP", 0xFFFFFFFF);
            return TurnStart{ false, true };
        }
    }
    return TurnStart{ true, false };
}

void status_on_turn_end(BattleCtx& ctx, Actor& a) {
    const int n = a.status.size();
    for (int i = 0; i < n; ++i) {
        const StatusDef& def = status_def(a.status[i].id);
        if (def.on_turn_end) def.on_turn_end(ctx, a, a.status[i]);
    }
}

} // namespace bt
