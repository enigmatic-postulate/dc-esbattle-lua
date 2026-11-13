// ui.cpp - port of battle/ui.lua
#include "ui.h"
#include "assets.h"
#include "input_probe.h"
#include "presentation.h"
#include "skills.h"
#include "targeting.h"

namespace bt {

static const real TARGET_ENTRY_LOCK_S = (real)0.25;
static const real CURSOR_BASE_W = 18, CURSOR_BASE_H = 18;

static const eng::Key UB_KEY[UB_COUNT] = { eng::KEY_FIRE, eng::KEY_LEFT, eng::KEY_RIGHT, eng::KEY_B, eng::KEY_Y };

void ui_init(BattleCtx& ctx) {
    ctx.ui.cursor_spr = asset_sprite("/rd/plasma.png");
    ctx.ui.bob = 0;
    probe_init(ctx);
}

// LEGACY: ui.lua treats "pressed" as "differs from the value sampled at the last
// reset()". If a button is held while reset() runs (e.g. across a turn change),
// its polarity is inverted until the next reset: *releasing* it then registers as
// a click. Ported as-is so the equivalence harness can prove parity; see REPORT.md
// for the recommended fix (plain now && !prev edge detection).
static void sample_idle(Ui& ui) {
    for (int i = 0; i < UB_COUNT; ++i) ui.idle[i] = probe_down(UB_KEY[i]);
}

static bool active(const Ui& ui, int b) { return probe_down(UB_KEY[b]) != ui.idle[b]; }

static bool click(Ui& ui, int b) {
    if (!active(ui, b)) { ui.lat[b] = false; return false; }
    if (ui.lat[b]) return false;
    ui.lat[b] = true;
    return true;
}

void ui_reset(BattleCtx& ctx) {
    Ui& ui = ctx.ui;
    ui.state = UiState::Home;
    ui.home_i = 0;
    ui.target_i = 0;
    ui.target_entry_lock = 0;
    for (int i = 0; i < UB_COUNT; ++i) ui.lat[i] = false;
    sample_idle(ui);
}

bool ui_update(BattleCtx& ctx, real dt, Actor& user, PendingAction& out) {
    Ui& ui = ctx.ui;
    ui.bob = ui.bob + dt;
    ui.target_entry_lock = maxv((real)0, ui.target_entry_lock - dt);

    // Only accept input on player turn (battle.cpp already guarantees this).
    if (!ctx.active || ctx.active->team != Team::Player) {
        for (int b = 0; b < UB_COUNT; ++b) click(ui, b);
        return false;
    }

    // Cancel. NOTE: click() must run every frame to keep the latch current.
    if (click(ui, UB_B) && ui.state == UiState::Target) {
        ui.state = UiState::Home;
        ui.target_entry_lock = 0;
        return false;
    }

    if (ui.state == UiState::Home) {
        if (click(ui, UB_LEFT))       { ui.home_i = (ui.home_i + 2) % 3; }
        else if (click(ui, UB_RIGHT)) { ui.home_i = (ui.home_i + 1) % 3; }

        if (click(ui, UB_Y)) { ui.home_i = 2; return false; }

        if (click(ui, UB_FIRE)) {
            if (ui.home_i == 0) {
                ui.state = UiState::Target;
                ui.target_i = 0;
                ui.target_entry_lock = TARGET_ENTRY_LOCK_S;
                return false;
            } else if (ui.home_i == 1) {
                ui.state = UiState::Home;
                out = PendingAction();        // AOE: targets intentionally empty; battle selects enemy_all
                out.skill = &skill(SK_FIREBALL);
                out.cursor_index = 0;
                return true;
            } else {
                ui.state = UiState::Home;
                out = PendingAction();
                out.skill = &skill(SK_GUARD);
                out.targets.push(&user);
                out.cursor_index = 0;
                return true;
            }
        }
    } else {   // UiState::Target
        const SkillDef& sk = skill(SK_ATTACK);
        ActorList cand = alive_enemies(ctx, user);
        if (cand.empty()) { ui.state = UiState::Home; return false; }
        const int n = cand.size();
        ui.target_i = clampv(ui.target_i, 0, n - 1);

        if (click(ui, UB_LEFT))       { ui.target_i = (ui.target_i - 1 + n) % n; }
        else if (click(ui, UB_RIGHT)) { ui.target_i = (ui.target_i + 1) % n; }

        // Lua short-circuit: A is only sampled once the entry lock has expired.
        if (ui.target_entry_lock == 0 && click(ui, UB_FIRE)) {
            out = PendingAction();
            out.skill = &sk;
            out.targets = select_targets(ctx, user, sk.target_mode, ui.target_i);
            out.cursor_index = ui.target_i;
            ui.state = UiState::Home;
            return true;
        }
    }
    return false;
}

static void draw_cursor(const BattleCtx& ctx, const Actor& a, real bob, real scale) {
    real ox, oy;
    pres_offset(ctx, &a, ox, oy);
    eng::sprite_draw(ctx.ui.cursor_spr, (float)(a.x + ox), (float)(a.y + oy - (a.h * (real)0.75) + bob),
                     (float)(CURSOR_BASE_W * scale), (float)(CURSOR_BASE_H * scale), 0, 0);
}

void ui_draw(BattleCtx& ctx) {
    Ui& ui = ctx.ui;
    Actor* act = ctx.active;
    if (!act || act->hp <= 0) return;
    if (!ui.cursor_spr) return;

    probe_draw(ctx);

    real bob = sin(ui.bob * (real)7.0) * (real)4.0;

    if (act->team != Team::Player) {   // enemy turn: marker over the acting enemy
        draw_cursor(ctx, *act, bob, (real)1.4);
        return;
    }

    if (ui.state == UiState::Home) {
        real ox, oy;
        pres_offset(ctx, act, ox, oy);
        real base_x = act->x + ox;
        real base_y = act->y + oy - (act->h * (real)0.75) + bob;
        const real spacing = 28;
        for (int i = 0; i < 3; ++i) {
            real s = (i == ui.home_i) ? (real)1.55 : (real)1.0;
            real px = base_x + (i - (real)1.0) * spacing;
            eng::sprite_draw(ui.cursor_spr, (float)px, (float)base_y,
                             (float)(CURSOR_BASE_W * s), (float)(CURSOR_BASE_H * s), 0, 0);
        }
        if (ui.home_i == 1) {   // fireball preview: cursor over ALL enemies
            ActorList enemies = alive_enemies(ctx, *act);
            for (Actor* e : enemies) draw_cursor(ctx, *e, bob, (real)1.25);
        }
    } else {
        ActorList cand = alive_enemies(ctx, *act);
        if (cand.empty()) return;
        ui.target_i = clampv(ui.target_i, 0, cand.size() - 1);   // LEGACY: draw() mutates state
        draw_cursor(ctx, *cand[ui.target_i], bob, (real)1.6);
    }
}

} // namespace bt
