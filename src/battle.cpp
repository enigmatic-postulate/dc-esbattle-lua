// battle.cpp - port of battle/battle.lua
// Each update() advances at most one state per tick, exactly like the Lua if/elseif chain;
// the tick-by-tick cadence is observable and preserved (verified by the harness).
#include "battle.h"
#include "ai.h"
#include "battle3d.h"
#include "fastmath.h"
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
static Rules g_rules;
static BattleView g_view = BattleView::Models3D;

Rules& rules() { return g_rules; }
bool fire_locked(const BattleCtx& ctx) { return g_rules.learned_skills && !(ctx.skills & SKILL_FIRE); }
void battle_set_view(BattleView v) { g_view = v; }
BattleView battle_view() { return g_view; }
static const int T_ANIMATE_MIN = 12;   // 0.20 s
static const int T_RESULT_HOLD = 30;   // banner shown this long before A is accepted

BattleCtx& battle_ctx() { return g_ctx; }
static bool g_auto_lose = false, g_auto_win = false;
void battle_set_auto(bool auto_lose, bool auto_win) { g_auto_lose = auto_lose; g_auto_win = auto_win; }
const BattleTally& battle_tally() { return g_ctx.tally; }

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
    if (g_view == BattleView::Models3D) battle3d_init();
}

void battle_begin(const EncounterDef& enc, const PartyState& party) {
    BattleCtx& ctx = g_ctx;
    ctx.pres.moves.clear();
    ctx.pres.flashes.clear();
    ctx.fx.floats.clear();
    spawn_encounter(ctx, enc, party);
    ctx.potions = party.potions;
    ctx.skills = party.skills;
    boss_begin(ctx);
    rebuild_order(ctx);
    ui_reset(ctx);
    ctx.pending = PendingAction();
    ctx.state = BattleState::Command;
    ctx.anim_ticks = 0;
    ctx.result_ticks = 0;
    ctx.outcome = BattleOutcome::None;
    ctx.events.clear();
    ctx.auto_lose = g_auto_lose; ctx.auto_win = g_auto_win;
    ctx.tally = BattleTally();
    if (g_view == BattleView::Models3D) battle3d_begin(ctx, enc);
}

BattleOutcome battle_outcome() { return g_ctx.outcome; }

int battle_potions() { return g_ctx.potions; }

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

    // Slice 3D-4: the 3D presentation turns this tick's events into sequences. While one plays the
    // state machine waits (the gates below) - they only change WHEN the next state comes, never
    // WHAT happens (tests/battle3d: turn-level equivalence with the sprite battle).
    bool busy = false;
    if (g_view == BattleView::Models3D) { battle3d_tick(ctx); busy = battle3d_busy(); }
    else ctx.events.clear();

    if (count_alive_team(ctx, Team::Enemy) == 0)       ctx.state = BattleState::Win;
    else if (count_alive_team(ctx, Team::Player) == 0) ctx.state = BattleState::Lose;

    switch (ctx.state) {
    case BattleState::Command: {
        if (busy) break;                                    // gate: burn ticks, deaths, the intro
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
            // Enemy AI (ai.cpp): basic attack on the first alive opponent; the Bone Warden's rotation.
            const EnemyChoice ch = enemy_choose(ctx, *ctx.active);
            const SkillDef& sk = skill((SkillIndex)ch.skill);
            ctx.pending = PendingAction();
            ctx.pending.skill = &sk;
            ctx.pending.targets = ch.targets;
            ctx.pending.cursor_index = 0;
            ctx.state = (sk.target_mode != TargetMode::Self && ch.targets.empty())
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
        {   // slice 3D-6: audio hooks (S12) and the results card's counts
            static const char* const SOUND[] = { "swing", "fireball", "shield", "potion" };
            const int si = skill_index(&sk);
            if (result.ok) eng::audio_event(si >= 0 && si < 4 ? SOUND[si] : "boss_skill");
            if (result.ok && (result.kind == ResultKind::Single || result.kind == ResultKind::Aoe)) eng::audio_event("hit");
            const bool hero = ctx.active->team == Team::Player;
            if (hero) { ++ctx.tally.hero_turns; if (result.ok && skill_index(&sk) == SK_POTION) ++ctx.tally.potions_used; }
            auto count = [&](const Actor* t, int d) {
                if (!t || d <= 0) return;
                if (t->team == Team::Enemy && hero) ctx.tally.dmg_dealt += d;
                if (t->team == Team::Player && !hero) ctx.tally.dmg_taken += d;
            };
            if (result.ok && result.kind == ResultKind::Single) count(result.target, result.dmg);
            else if (result.ok && result.kind == ResultKind::Aoe) for (const Hit& h : result.hits) count(h.target, h.dmg);
        }
        if (sk.present) sk.present(ctx, *ctx.active, result, ctx.pending.targets);
        {   // presentation event (read by the 3D view only)
            BattleEvent ev;
            ev.kind = EvKind::Action; ev.user = ctx.active; ev.skill = skill_index(&sk); ev.result = result.kind; ev.dmg = 0;
            if (result.ok && result.kind == ResultKind::Single && result.target) ev.hits.push(Hit{ result.target, result.dmg });
            else if (result.ok && (result.kind == ResultKind::Aoe || result.kind == ResultKind::Summon)) ev.hits = result.hits;
            else if (result.ok && result.kind == ResultKind::Self && result.target) ev.hits.push(Hit{ result.target, result.dmg });   // potion: -healed
            ctx.events.push(ev);
        }
        boss_after(ctx);                                    // a fallen boss takes his minions

        ctx.anim_ticks = 0;
        ctx.state = BattleState::Animate;
        break;
    }

    case BattleState::Animate:
        ctx.anim_ticks = ctx.anim_ticks + 1;
        if (ctx.anim_ticks >= T_ANIMATE_MIN && pres_done(ctx) && !busy) ctx.state = BattleState::TurnAdvance;
        break;

    case BattleState::TurnAdvance:
        if (ctx.active) {   // end-of-turn bookkeeping for the actor that just acted
            status_on_turn_end(ctx, *ctx.active);
            status_tick_turns(ctx, *ctx.active);
        }
        ctx.active = advance_turn(ctx);
        boss_after(ctx);                                    // burn may have felled the boss
        ui_reset(ctx);
        ctx.pending = PendingAction();
        ctx.state = BattleState::Command;
        break;

    case BattleState::Win:
    case BattleState::Lose:
        // Result banner; A (after a short hold) confirms and hands control back to the game.
        if (busy) break;                                    // gate: the killing blow's sequence
        ctx.result_ticks = ctx.result_ticks + 1;
        if (ctx.outcome == BattleOutcome::None && ctx.state == BattleState::Lose && ctx.auto_lose) { ctx.outcome = BattleOutcome::Lost; break; }
        if (ctx.outcome == BattleOutcome::None && ctx.state == BattleState::Win && ctx.auto_win) {
            if (ctx.result_ticks >= BOSS_WIN_HOLD) ctx.outcome = BattleOutcome::Won;
            break;
        }
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
    if (ctx.result_ticks >= T_RESULT_HOLD && (ctx.result_ticks / 20) % 2 == 0 && !(won && ctx.auto_win))
        font_draw_centered(320, 256, "PRESS A", 2, 0xFFF4F4F4, true, 8.5f);
}

void battle_draw() {
    BattleCtx& ctx = g_ctx;
    if (g_view == BattleView::Models3D) { battle3d_draw(ctx); return; }
    draw_backdrop();
    for (Actor& a : ctx.actors) {
        if (a.hp > 0) {
            real ox, oy;
            pres_offset(ctx, &a, ox, oy);
            real rot = 0;
            // LEGACY: anim_ticks only advances in Animate, so enemies freeze mid-wobble between actions.
            if (a.team == Team::Enemy) rot = rsin((real)ctx.anim_ticks * TICK_DT * (real)6.0) * (real)0.10;
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
    if (ctx.state == BattleState::Win || (ctx.state == BattleState::Lose && !ctx.auto_lose)) draw_result_banner(ctx);
}

} // namespace bt
