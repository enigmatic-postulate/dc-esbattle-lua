// battle.cpp - port of battle/battle.lua
// Each update() advances at most one state per tick, exactly like the Lua if/elseif chain;
// the tick-by-tick cadence is observable and preserved (verified by the harness).
#include "battle.h"
#include "assets.h"
#include "chars.h"
#include "font.h"
#include "fx.h"
#include "hpbar.h"
#include "icons.h"
#include "input.h"
#include "presentation.h"
#include "skills.h"
#include "status.h"
#include "targeting.h"
#include "turn_system.h"
#include "ui.h"

namespace bt {

static BattleCtx g_ctx;
static const int T_ANIMATE_MIN = 12;   // 0.20 s
static const int T_RESULT_HOLD = 30;   // banner shown this long before A is accepted

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

void battle_init() {
    pres_init(g_ctx);
    ui_init(g_ctx);
}

void battle_begin(const EncounterDef& enc, const PartyState& party) {
    BattleCtx& ctx = g_ctx;
    ctx.pres.moves.clear();
    ctx.pres.flashes.clear();
    ctx.fx.floats.clear();
    spawn_encounter(ctx, enc, party);
    rebuild_order(ctx);
    ui_reset(ctx);
    ctx.pending = PendingAction();
    ctx.state = BattleState::Command;
    ctx.anim_ticks = 0;
    ctx.result_ticks = 0;
    ctx.outcome = BattleOutcome::None;
}

BattleOutcome battle_outcome() { return g_ctx.outcome; }

int battle_hero_hp() {
    for (const Actor& a : g_ctx.actors) if (a.team == Team::Player) return a.hp;
    return 0;
}

void battle_update() {
    BattleCtx& ctx = g_ctx;
    ctx.idle_ticks = ctx.idle_ticks + 1;
    pres_update(ctx);
    fx_update(ctx);

    for (Actor& a : ctx.actors)
        if (a.hurt_ticks > 0) a.hurt_ticks = a.hurt_ticks - 1;

    if (count_alive_team(ctx, Team::Enemy) == 0)       ctx.state = BattleState::Win;
    else if (count_alive_team(ctx, Team::Player) == 0) ctx.state = BattleState::Lose;

    switch (ctx.state) {
    case BattleState::Command: {
        if (!alive(ctx.active)) { ctx.state = BattleState::TurnAdvance; return; }

        if (ctx.active->team == Team::Player) {
            PendingAction action;
            if (ui_update(ctx, *ctx.active, action)) {
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

        ctx.anim_ticks = 0;
        ctx.state = BattleState::Animate;
        break;
    }

    case BattleState::Animate:
        ctx.anim_ticks = ctx.anim_ticks + 1;
        if (ctx.anim_ticks >= T_ANIMATE_MIN && pres_done(ctx)) ctx.state = BattleState::TurnAdvance;
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
        // Result banner; A (after a short hold) confirms and hands control back to the game.
        ctx.result_ticks = ctx.result_ticks + 1;
        if (ctx.outcome == BattleOutcome::None && ctx.result_ticks >= T_RESULT_HOLD && input_pressed(eng::KEY_FIRE))
            ctx.outcome = (ctx.state == BattleState::Win) ? BattleOutcome::Won : BattleOutcome::Lost;
        break;
    }
}

static void draw_backdrop() {
    eng::gfx_rect(0, 0, 640, 300, 0xFF1A1C2C, 0.5f);     // sky
    eng::gfx_rect(0, 300, 640, 180, 0xFF24403A, 0.5f);   // ground
    eng::gfx_rect(0, 296, 640, 4, 0xFF38B764, 0.5f);     // horizon line
}

static void draw_result_banner(const BattleCtx& ctx) {
    const bool won = ctx.state == BattleState::Win;
    eng::gfx_rect_tr(0, 196, 640, 96, 0xC0000000, 8.0f);
    font_draw_centered(320, 210, won ? "VICTORY!" : "DEFEAT...", 4, won ? 0xFFFFCD75 : 0xFFEF7D57, true, 8.5f);
    if (ctx.result_ticks >= T_RESULT_HOLD && (ctx.result_ticks / 20) % 2 == 0)
        font_draw_centered(320, 256, "PRESS A", 2, 0xFFF4F4F4, true, 8.5f);
}

void battle_draw() {
    BattleCtx& ctx = g_ctx;
    draw_backdrop();
    for (Actor& a : ctx.actors) {
        if (a.hp > 0) {
            real ox, oy;
            pres_offset(ctx, &a, ox, oy);
            real rot = 0;
            // LEGACY: anim_ticks only advances in Animate, so enemies freeze mid-wobble between actions.
            if (a.team == Team::Enemy) rot = sin((real)ctx.anim_ticks * TICK_DT * (real)6.0) * (real)0.10;
            int col = (a.sheet == SHEET_SLIME) ? (ctx.idle_ticks / 20) % 2 : (int)FRAME_STAND;   // slime squash idle
            char_draw(a.sheet, a.facing, col, a.x + ox, a.y + oy, a.w, rot, 0);
        }
        if (a.hurt_ticks > 0)   // drawn even for the dead: brief marker on the killing blow
            icon_draw(ICON_HURT, a.x + a.w * (real)0.5, a.y - a.h * (real)0.5, ICON_1X);
    }

    hpbar_draw_all(ctx);   // bars under UI
    pres_draw(ctx);        // flash markers before fx
    ui_draw(ctx);          // menus/cursor
    fx_draw(ctx);          // floating numbers always last
    if (ctx.state == BattleState::Win || ctx.state == BattleState::Lose) draw_result_banner(ctx);
}

} // namespace bt
