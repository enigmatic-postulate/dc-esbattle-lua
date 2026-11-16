// ui.cpp - port of battle/ui.lua
#include "ui.h"
#include "fastmath.h"
#include "font.h"
#include "icons.h"
#include "input.h"
#include "input_probe.h"
#include "presentation.h"
#include "skills.h"
#include "targeting.h"

namespace bt {

static const int TARGET_ENTRY_LOCK = 16;   // ticks (Lua 0.25 s)
// Layout (px). Icons are drawn at multiples of 16 to stay crisp.
static const real RING_SPACING  = 44;   // command ring: Attack / Fireball / Guard
static const real RING_LIFT     = 28;   // ring centre above the actor's top edge
static const real MARKER_LIFT   = 20;   // target cursor / turn marker above the top edge
static const Icon RING_ICON[3]  = { ICON_SWORD, ICON_FLAME, ICON_SHIELD };
static const char* const RING_NAME[3] = { "ATTACK", "FIRE", "GUARD" };

static const eng::Key UB_KEY[UB_COUNT] = { eng::KEY_FIRE, eng::KEY_LEFT, eng::KEY_RIGHT, eng::KEY_B, eng::KEY_Y };

int ui_command_count() { return rules().potions ? 4 : 3; }

void ui_init(BattleCtx& ctx) {
    ctx.ui.bob_ticks = 0;
    probe_init(ctx);
}

// A "click" is a press edge on this tick (input.cpp). This replaces ui.lua's idle-
// baseline comparison, which only existed to cope with main.cpp's inverted button
// polarity (fixed in the KOS backend in the same change; see REPORT.md §5).
static bool click(int b) { return input_pressed(UB_KEY[b]); }

void ui_reset(BattleCtx& ctx) {
    Ui& ui = ctx.ui;
    ui.state = UiState::Home;
    ui.home_i = 0;
    ui.target_i = 0;
    ui.target_entry_lock = 0;
}

// Input per UI state. B always goes back exactly one step:
//   Home      : L/R choose command, Y jumps to Guard, A = Attack -> Target, Fire -> TargetAll, Guard -> cast
//   Target    : L/R choose enemy, A attacks it, B -> Home (Attack still highlighted)
//   TargetAll : A casts on every enemy, B -> Home (Fire still highlighted)
// A in Target/TargetAll is ignored for TARGET_ENTRY_LOCK ticks so a double-tap can't skip the step.
bool ui_update(BattleCtx& ctx, Actor& user, PendingAction& out) {
    Ui& ui = ctx.ui;
    ui.bob_ticks = ui.bob_ticks + 1;
    ui.target_entry_lock = maxv(0, ui.target_entry_lock - 1);

    // Only accept input on player turn (battle.cpp already guarantees this).
    if (!ctx.active || ctx.active->team != Team::Player) return false;

    switch (ui.state) {
    case UiState::Home: {
        // 3 commands (Attack, Fire, Guard), or 4 with the Potion (GDD 11.1; rules().potions)
        const int n = ui_command_count();
        if (click(UB_LEFT))       { ui.home_i = (ui.home_i + n - 1) % n; }
        else if (click(UB_RIGHT)) { ui.home_i = (ui.home_i + 1) % n; }

        if (click(UB_Y)) { ui.home_i = 2; return false; }

        if (click(UB_FIRE)) {
            if (ui.home_i == 0) {
                ui.state = UiState::Target;
                ui.target_i = 0;
                ui.target_entry_lock = TARGET_ENTRY_LOCK;
            } else if (ui.home_i == 1) {
                ui.state = UiState::TargetAll;            // confirm step: every enemy is marked
                ui.target_entry_lock = TARGET_ENTRY_LOCK;
            } else if (ui.home_i == 2) {
                out = PendingAction();
                out.skill = &skill(SK_GUARD);
                out.targets.push(&user);
                out.cursor_index = 0;
                return true;
            } else if (ctx.potions > 0) {                 // Potion: greyed (A does nothing) when none are left
                out = PendingAction();
                out.skill = &skill(SK_POTION);
                out.targets.push(&user);
                out.cursor_index = 0;
                return true;
            }
        }
        return false;
    }

    case UiState::Target: {
        if (click(UB_B)) { ui.state = UiState::Home; ui.target_entry_lock = 0; return false; }
        const SkillDef& sk = skill(SK_ATTACK);
        ActorList cand = alive_enemies(ctx, user);
        if (cand.empty()) { ui.state = UiState::Home; return false; }
        const int n = cand.size();
        ui.target_i = clampv(ui.target_i, 0, n - 1);

        if (click(UB_LEFT))       { ui.target_i = (ui.target_i - 1 + n) % n; }
        else if (click(UB_RIGHT)) { ui.target_i = (ui.target_i + 1) % n; }

        if (ui.target_entry_lock == 0 && click(UB_FIRE)) {
            out = PendingAction();
            out.skill = &sk;
            out.targets = select_targets(ctx, user, sk.target_mode, ui.target_i);
            out.cursor_index = ui.target_i;
            ui.state = UiState::Home;
            return true;
        }
        return false;
    }

    case UiState::TargetAll:
        if (click(UB_B)) { ui.state = UiState::Home; ui.target_entry_lock = 0; return false; }
        if (ui.target_entry_lock == 0 && click(UB_FIRE)) {
            out = PendingAction();                // AOE: targets left empty; battle selects enemy_all
            out.skill = &skill(SK_FIREBALL);
            out.cursor_index = 0;
            ui.state = UiState::Home;
            return true;
        }
        return false;
    }
    return false;
}

static void draw_marker(const BattleCtx& ctx, const Actor& a, real bob, Icon icon, real size = ICON_2X) {
    real ox, oy;
    pres_offset(ctx, &a, ox, oy);
    icon_draw(icon, a.x + ox, a.y + oy - a.h * (real)0.5 - MARKER_LIFT + bob, size);
}

// Bottom prompt bar: [glyph] LABEL pairs, so every available button is visible.
typedef UiPrompt Prompt;
void ui_draw_prompts(const UiPrompt* p, int n) {
    const real y = 446, x0 = 250;
    real x = x0;
    for (int i = 0; i < n; ++i) {
        x += 8;
        icon_draw(p[i].a, x + 8, y + 8, ICON_1X); x += 18;
        if (p[i].b != ICON_COUNT) { icon_draw(p[i].b, x + 8, y + 8, ICON_1X); x += 18; }
        font_draw(x + 2, y + 1, p[i].text, 2, 0xFFF4F4F4);
        x += 2 + font_width(p[i].text, 2) + 6;
    }
    eng::gfx_rect_tr((float)x0, (float)(y - 4), (float)(x - x0 + 4), 24, 0xA0000000, 0.9f);   // under icons/text
}

void ui_draw(BattleCtx& ctx) {
    Ui& ui = ctx.ui;
    Actor* act = ctx.active;
    if (!act || act->hp <= 0) return;

    probe_draw(ctx);

    real bob = rsin((real)ui.bob_ticks * TICK_DT * (real)7.0) * (real)4.0;

    if (act->team != Team::Player) {   // enemy turn: marker over the acting enemy
        draw_marker(ctx, *act, bob, ICON_ENEMY_TURN);
        return;
    }

    // Player selection indicators (ring, previews, target cursor) only while choosing a
    // command; the Lua build kept drawing them through resolve/animate, covering hit sparks.
    if (ctx.state != BattleState::Command) return;

    real ox, oy;
    pres_offset(ctx, act, ox, oy);
    const real base_x = act->x + ox;
    const real base_y = act->y + oy - act->h * (real)0.5 - RING_LIFT + bob;
    ActorList enemies = alive_enemies(ctx, *act);

    if (ui.state == UiState::Home) {
        for (int i = 0; i < 3; ++i) {
            real size = (i == ui.home_i) ? ICON_3X : ICON_2X;   // selected command is enlarged
            icon_draw(RING_ICON[i], base_x + (i - (real)1.0) * RING_SPACING, base_y, size);
        }
        font_draw_centered(base_x, base_y - 48, ui.home_i < 3 ? RING_NAME[ui.home_i] : "POTION", 2, 0xFFFFCD75);
        if (ui.home_i == 1)   // preview: small markers over every enemy Fire would hit
            for (Actor* e : enemies) draw_marker(ctx, *e, 0, ICON_TARGET, ICON_1X);
        static const Prompt P[] = { { ICON_PAD_A, ICON_COUNT, "SELECT" },
                                    { ICON_PAD_LEFT, ICON_PAD_RIGHT, "CHOOSE" },
                                    { ICON_PAD_Y, ICON_COUNT, "GUARD" } };
        ui_draw_prompts(P, 3);
        return;
    }

    // Target / TargetAll: the chosen command stays visible above the hero.
    const int cmd = ui.state == UiState::Target ? 0 : 1;
    icon_draw(RING_ICON[cmd], base_x, base_y, ICON_2X);
    font_draw_centered(base_x, base_y - 40, RING_NAME[cmd], 2, 0xFFFFCD75);

    if (ui.state == UiState::Target) {
        if (enemies.empty()) return;
        ui.target_i = clampv(ui.target_i, 0, enemies.size() - 1);   // LEGACY: draw() mutates state
        draw_marker(ctx, *enemies[ui.target_i], bob, ICON_TARGET);
        static const Prompt P[] = { { ICON_PAD_A, ICON_COUNT, "ATTACK" },
                                    { ICON_PAD_LEFT, ICON_PAD_RIGHT, "TARGET" },
                                    { ICON_PAD_B, ICON_COUNT, "BACK" } };
        ui_draw_prompts(P, 3);
    } else {
        for (Actor* e : enemies) draw_marker(ctx, *e, bob, ICON_TARGET);   // every enemy will be hit
        font_draw_centered(enemies.empty() ? 480 : enemies[0]->x, 100, "ALL ENEMIES", 2, 0xFFEF7D57);
        static const Prompt P[] = { { ICON_PAD_A, ICON_COUNT, "CAST FIRE" },
                                    { ICON_PAD_B, ICON_COUNT, "BACK" } };
        ui_draw_prompts(P, 2);
    }
}

} // namespace bt
