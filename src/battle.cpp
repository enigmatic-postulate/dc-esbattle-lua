// battle.cpp - port of battle/battle.lua
// Each update() advances at most one state per frame, exactly like the Lua
// if/elseif chain; the frame-by-frame cadence is observable and preserved.
#include "battle.h"
#include "assets.h"
#include "encounters.h"
#include "fx.h"
#include "hpbar.h"
#include "presentation.h"
#include "skills.h"
#include "status.h"
#include "targeting.h"
#include "turn_system.h"
#include "ui.h"

namespace bt {

static BattleCtx   g_ctx;
static eng::Sprite g_plasma = 0;

BattleCtx& battle_ctx() { return g_ctx; }

const char* battle_state_name(BattleState s) {
    switch (s) {
    case BattleState::Command:     return "command";
    case BattleState::Resolve:     return "resolve";
    case BattleState::Animate:     return "animate";
    case BattleState::TurnAdvance: return "turn_advance";
    case BattleState::Win:         return "win";
    case BattleState::Lose:        return "lose";
    }
    return "?";
}

static void rebuild_order(BattleCtx& ctx) {
    build_order(ctx);
    ctx.turn_i = 0;
    ctx.active = ctx.order.empty() ? nullptr : ctx.order[0];
    if (ctx.active) start_turn(ctx, *ctx.active);   // LEGACY: result ignored (a stunned first actor still acts)
}

void battle_start() {
    BattleCtx& ctx = g_ctx;
    spawn_test_slimes(ctx);
    rebuild_order(ctx);
    ui_reset(ctx);
    ctx.pending = PendingAction();
    ctx.state = BattleState::Command;
    ctx.anim_t = 0;
}

void battle_init() {
    // Module-load order mirrors the Lua require() chain.
    pres_init(g_ctx);
    ui_init(g_ctx);
    hpbar_init();
    g_plasma = asset_sprite("/rd/plasma.png");
    // LEGACY: battle.lua drew a stray 12x12 stamp at module load (outside draw()),
    // which the engine flushed during the first frame. Kept for trace parity.
    eng::sprite_draw(g_plasma, 30, 30, 12, 12, 0);
    battle_start();
}

void battle_update(real dt) {
    BattleCtx& ctx = g_ctx;
    pres_update(ctx, dt);
    fx_update(ctx, dt);

    for (Actor& a : ctx.actors)
        if (a.hurt_t > 0) a.hurt_t = a.hurt_t - dt;

    if (count_alive_team(ctx, Team::Enemy) == 0)       ctx.state = BattleState::Win;
    else if (count_alive_team(ctx, Team::Player) == 0) ctx.state = BattleState::Lose;

    switch (ctx.state) {
    case BattleState::Command: {
        if (!alive(ctx.active)) { ctx.state = BattleState::TurnAdvance; return; }

        if (ctx.active->team == Team::Player) {
            PendingAction action;
            if (ui_update(ctx, dt, *ctx.active, action)) {
                ctx.pending = action;
                // Only skip for "none" (AOE deliberately arrives with empty targets).
                ctx.state = (ctx.pending.skill->target_mode == TargetMode::None)
                                ? BattleState::TurnAdvance : BattleState::Resolve;
            }
        } else {
            // Enemy AI: basic attack on the first alive opponent.
            const SkillDef& sk = skill(SK_ATTACK);
            ActorList targets = select_targets(ctx, *ctx.active, sk.target_mode, 0);
            ctx.pending = PendingAction();
            ctx.pending.skill = &sk;
            ctx.pending.targets = targets;
            ctx.pending.cursor_index = 0;
            ctx.state = (sk.target_mode != TargetMode::Self && targets.empty())
                            ? BattleState::TurnAdvance : BattleState::Resolve;
        }
        break;
    }

    case BattleState::Resolve: {
        if (!ctx.pending.skill) { ctx.state = BattleState::TurnAdvance; return; }
        const SkillDef& sk = *ctx.pending.skill;

        // UI left targets empty (AOE): select them now from target_mode.
        if (ctx.pending.targets.empty() && sk.target_mode != TargetMode::Self)
            ctx.pending.targets = select_targets(ctx, *ctx.active, sk.target_mode, ctx.pending.cursor_index);

        if (sk.target_mode != TargetMode::Self && ctx.pending.targets.empty()) {
            ctx.pending = PendingAction();
            ui_reset(ctx);
            ctx.state = BattleState::Command;
            return;
        }

        SkillResult result;
        if (sk.resolve) result = sk.resolve(ctx, *ctx.active, ctx.pending.targets);
        if (sk.present) sk.present(ctx, *ctx.active, result, ctx.pending.targets);

        ctx.anim_t = 0;
        ctx.state = BattleState::Animate;
        break;
    }

    case BattleState::Animate:
        ctx.anim_t = ctx.anim_t + dt;
        if (ctx.anim_t >= (real)0.20 && pres_done(ctx)) ctx.state = BattleState::TurnAdvance;
        break;

    case BattleState::TurnAdvance:
        if (ctx.active) {   // end-of-turn bookkeeping for the actor that just acted
            status_on_turn_end(ctx, *ctx.active);
            status_tick_turns(ctx, *ctx.active);
        }
        ctx.active = advance_turn(ctx);
        ui_reset(ctx);
        ctx.pending = PendingAction();
        ctx.state = BattleState::Command;
        break;

    case BattleState::Win:
    case BattleState::Lose:
        break;   // LEGACY: terminal; nothing handles these states yet
    }
}

void battle_draw() {
    BattleCtx& ctx = g_ctx;
    for (Actor& a : ctx.actors) {
        if (a.hp > 0) {
            real ox, oy;
            pres_offset(ctx, &a, ox, oy);
            real rot = 0;
            // LEGACY: anim_t only advances in Animate, so enemies freeze mid-wobble between actions.
            if (a.team == Team::Enemy) rot = sin(ctx.anim_t * (real)6.0) * (real)0.10;
            eng::sprite_draw(a.spr, (float)(a.x + ox), (float)(a.y + oy), (float)a.w, (float)a.h,
                             (float)rot, a.team == Team::Enemy ? 2 : 0);
        }
        if (a.hurt_t > 0)   // drawn even for the dead: brief marker on the killing blow
            eng::sprite_draw(g_plasma, (float)a.x, (float)(a.y - (a.h * (real)0.9)), 12, 12, 0, 0);
    }

    hpbar_draw_all(ctx);   // bars under UI
    pres_draw(ctx);        // flash markers before fx
    ui_draw(ctx);          // menus/cursor
    fx_draw(ctx);          // floating numbers always last
}

} // namespace bt
