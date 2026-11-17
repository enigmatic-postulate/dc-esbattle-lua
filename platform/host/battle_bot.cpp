// battle_bot.cpp - host harness only: a battle bot that plays through the real UI, and a
// turn-level log of every rules-visible change (slice 3D-4, stage 0.2 / D1).
//
// Why: the 3D battle changes *when* things happen (sequences take 1-3 s instead of 0.2 s), so
// tick-exact traces can no longer be compared with the sprite battle. What must not change is
// *what* happens. The turn log records only rules state - who acts, which skill, which targets,
// the damage, every actor's HP and statuses, the turn order - one line per change, with no tick
// numbers. The same bot policy on two builds must give byte-identical logs.
//
// The bot is closed-loop: it presses a button for one tick, releases it for one tick, then checks
// the UI state to see whether the press was taken. A press the game ignored (e.g. while a 3D
// sequence is playing) is simply retried, so the bot works unchanged on the sprite battle (the
// baseline) and on the 3D battle. It only uses BattleCtx fields both builds have.
//
// Policies (--bot NAME):
//   attack:K    always Attack the K-th living enemy (clamped; 0 = leftmost)
//   fire        always Fire
//   guard       always Guard
//   cycle:SEQ   repeat SEQ, one command per hero turn: A/a = Attack 0, 1..3 = Attack 1..3,
//               F = Fire, G = Guard, Y = Guard via the Y shortcut, b = enter Attack then back
//               out with B (then Fire), c = enter Fire then back out with B (then Attack 0)
//   random:SEED seeded choice among Attack k / Fire / Guard each turn
//   (the slice's extra commands, when the build has them: P = Potion)
//   boss:MODE:SEED  the Bone Warden balance bots (GDD 12.6), each varied by a seeded 10 % chance per turn
//               of a random command instead (50 seeds -> a win rate):
//               good     Potion below 10 HP, Guard while he charges, Fire when two minions stand, else
//                        Attack the Warden
//               naive    Potion below 10 HP, else Attack the Warden
//               noguard  as good, without the Guard
//   smart       (slice 3D-5, the story routes) Potion below 10 HP, Fire against 2 or more enemies when it is
//               learned, else Attack the first enemy
// Greyed commands (slice 3D-5, task 0.3): a Potion with none left or a Fire not yet learned would be
// ignored by the UI and stall the bot; such a choice becomes Attack on the first enemy.
#include "../../src/battle.h"
#include "../../src/game.h"
#include "../../src/status.h"
#include "../../src/skills.h"
#include "../../src/targeting.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

enum Btn { B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_A = 16, B_B = 32, B_Y = 64 };

struct Bot {
    char     policy[64] = "attack:0";
    uint32_t rng = 1;
    int      turn = 0;          // hero commands issued
    // current plan: a list of steps, executed one at a time
    char     plan[16];
    int      plan_n = 0, plan_i = 0;
    int      phase = 0;         // 0 = idle/press, 1 = release, 2 = verify
    uint32_t pressing = 0;
    int      wait = 0;          // result banner: A is tapped every 4th tick
    bool     active = false;
    int      start_wait = 0;    // --bot-wait N: no presses for the first N ticks (lets an intro play)
    int      linger = 0;        // --bot-linger N: wait N ticks on the result banner before confirming
    bool     hold_a = false;    // --bot-hold-a: hold A whenever a 3D sequence plays (the 2x speed-up)
    int      banner_ticks = 0;
    int      ticks = 0;
    // UI snapshot before the current press (to verify its effect)
    int      s_state, s_home, s_target, s_bstate;
} g_bot;

uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }

// One hero command -> a step string. Steps: 'A' press A, 'B' press B, 'R' press Right,
// 'L' press Left, 'Y' press Y, 'w' wait for the target-entry lock to run out.
void make_plan(const char* cmd_steps) {
    snprintf(g_bot.plan, sizeof g_bot.plan, "%s", cmd_steps);
    g_bot.plan_n = (int)strlen(g_bot.plan);
    g_bot.plan_i = 0;
    g_bot.phase = 0;
}

char choose_command(const bt::BattleCtx& ctx) {
    const char* p = g_bot.policy;
    const int n_enemies = bt::count_alive_team(ctx, bt::Team::Enemy);
    if (!strncmp(p, "attack:", 7)) { int k = atoi(p + 7); return (char)('0' + (k < n_enemies ? k : n_enemies - 1)); }
    if (!strcmp(p, "fire")) return 'F';
    if (!strcmp(p, "guard")) return 'G';
    if (!strncmp(p, "cycle:", 6)) {
        const char* seq = p + 6;
        const int len = (int)strlen(seq);
        char c = len ? seq[g_bot.turn % len] : 'A';
        if (c == 'A' || c == 'a') return '0';
        if (c >= '1' && c <= '3') return (char)('0' + (c - '0' < n_enemies ? c - '0' : n_enemies - 1));
        return c;
    }
    if (!strncmp(p, "boss:", 5)) {
        const char* mode = p + 5;
        const bt::Actor* hero = ctx.active;
        const bt::Actor* boss = nullptr;
        int minions = 0, boss_k = 0, k = 0;
        for (const bt::Actor& a : ctx.actors) {
            if (a.team != bt::Team::Enemy || a.hp <= 0) continue;
            if (a.role == bt::ROLE_BOSS) { boss = &a; boss_k = k; }
            if (a.role == bt::ROLE_MINION) ++minions;
            ++k;
        }
        if ((lcg(g_bot.rng) % 100) < 10) {                  // varied play: a random command
            const uint32_t r = lcg(g_bot.rng) % 4;
            if (r == 0) return (char)('0' + (int)(lcg(g_bot.rng) % (uint32_t)n_enemies));
            return r == 1 ? 'F' : (r == 2 ? 'G' : '0' + boss_k);
        }
        if (hero && hero->hp < 10 && ctx.potions > 0) return 'P';
        if (!boss) return n_enemies >= 2 ? 'F' : '0';                      // a patrol (the field flow): Fire groups
        if (!strncmp(mode, "naive", 5)) return (char)('0' + boss_k);     // naive: hits the boss, nothing else
        const bool charging = boss && bt::status_has(*boss, bt::StatusId::Charge);
        if (charging && strncmp(mode, "noguard", 7)) return 'G';
        if (minions >= 2) return 'F';
        return (char)('0' + boss_k);
    }
    if (!strncmp(p, "random:", 7)) {
        uint32_t r = lcg(g_bot.rng) % 6;
        if (r < 3) return (char)('0' + (int)(lcg(g_bot.rng) % (uint32_t)n_enemies));
        return r < 5 ? 'F' : 'G';
    }
    if (!strcmp(p, "smart")) {
        const bt::Actor* hero = ctx.active;
        if (hero && hero->hp < 10 && ctx.potions > 0) return 'P';
        if (n_enemies >= 2 && !bt::fire_locked(ctx)) return 'F';
        return '0';
    }
    return '0';
}

// A command the UI would ignore (greyed) becomes Attack on the first enemy (task 0.3).
char with_fallback(const bt::BattleCtx& ctx, char c) {
    if (c == 'P' && ctx.potions <= 0) return '0';
    if ((c == 'F' || c == 'c' || c == 'b') && bt::fire_locked(ctx)) return '0';
    return c;
}

void plan_for(char c) {
    static char buf[16];
    if (c >= '0' && c <= '9') {                 // Attack the k-th living enemy
        int k = c - '0', i = 0;
        buf[i++] = 'A'; buf[i++] = 'w';
        for (int j = 0; j < k; ++j) buf[i++] = 'R';
        buf[i++] = 'X';                          // confirming A (after the lock)
        buf[i] = 0;
        make_plan(buf);
    } else if (c == 'F') make_plan("RAwX");     // Fire, confirm step
    else if (c == 'G') make_plan("RRA");        // Guard via the ring
    else if (c == 'Y') make_plan("YA");         // Guard via the Y shortcut
    else if (c == 'b') make_plan("AwBRAwX");    // Attack, back out, Fire
    else if (c == 'c') make_plan("RAwBLAwX");   // Fire, back out, Attack 0
    else if (c == 'P') make_plan("LA");         // Potion (4-command ring: Left from Attack)
    else make_plan("AwX");
}

bool hero_turn(const bt::BattleCtx& ctx) {
    return ctx.state == bt::BattleState::Command && ctx.active && ctx.active->team == bt::Team::Player && ctx.active->hp > 0;
}

void snapshot(const bt::BattleCtx& ctx) {
    g_bot.s_state = (int)ctx.ui.state; g_bot.s_home = ctx.ui.home_i; g_bot.s_target = ctx.ui.target_i;
    g_bot.s_bstate = (int)ctx.state;
}

bool press_taken(const bt::BattleCtx& ctx) {
    return (int)ctx.ui.state != g_bot.s_state || ctx.ui.home_i != g_bot.s_home || ctx.ui.target_i != g_bot.s_target ||
           (int)ctx.state != g_bot.s_bstate;
}

} // namespace

void bot_init(const char* policy) {
    snprintf(g_bot.policy, sizeof g_bot.policy, "%s", policy);
    if (!strncmp(policy, "random:", 7)) g_bot.rng = (uint32_t)strtoul(policy + 7, nullptr, 10) * 2654435761u + 7u;
    if (!strncmp(policy, "boss:", 5)) {
        const char* c = strchr(policy + 5, ':');
        g_bot.rng = (c ? (uint32_t)strtoul(c + 1, nullptr, 10) : 1u) * 2654435761u + 11u;
    }
    g_bot.active = true;
}

bool bot_enabled() { return g_bot.active; }
void bot_set_wait(int ticks) { g_bot.start_wait = ticks; }
void bot_set_linger(int ticks) { g_bot.linger = ticks; }
void bot_set_hold_a(bool on) { g_bot.hold_a = on; }

// Buttons for this tick (call before game_tick).
uint32_t bot_buttons() {
    const bt::BattleCtx& ctx = bt::battle_ctx();
    Bot& b = g_bot;
    if (b.ticks++ < b.start_wait) return 0;
    if (bt::game_mode() != bt::GameMode::Battle) { b.plan_n = 0; b.phase = 0; return 0; }
    // result banner: keep tapping A (the game accepts it after its hold time)
    if (ctx.state == bt::BattleState::Win || ctx.state == bt::BattleState::Lose) {
        b.plan_n = 0;
        if (b.banner_ticks++ < b.linger) return 0;
        b.wait = (b.wait + 1) % 4;
        return b.wait == 0 ? B_A : 0;
    }
    if (b.phase == 1) { b.phase = 2; return 0; }                  // release
    if (b.phase == 2) {                                           // verify the press
        b.phase = 0;
        if (press_taken(ctx)) {
            ++b.plan_i;
            if (b.plan_i >= b.plan_n) b.plan_n = 0;               // command issued
        }
        // else: not taken (presentation busy, lock, ...): the same step is pressed again
    }
    if (!hero_turn(ctx) || bt::count_alive_team(ctx, bt::Team::Enemy) == 0) {
        b.plan_n = 0;
        // while a sequence plays (not during the intro, where A skips): hold A for 2x
        return (b.hold_a && bt::battle_view() == bt::BattleView::Models3D && ctx.state != bt::BattleState::Command) ? B_A : 0;
    }
    if (b.plan_n == 0) {
        if (ctx.ui.state != bt::UiState::Home) return 0;
        plan_for(with_fallback(ctx, choose_command(ctx)));
        ++b.turn;
    }
    char step = b.plan[b.plan_i];
    if (step == 'w') {                                            // wait for the entry lock
        if (ctx.ui.target_entry_lock > 0) return 0;
        ++b.plan_i;
        step = b.plan[b.plan_i];
    }
    uint32_t btn = 0;
    switch (step) {
    case 'A': case 'X': btn = B_A; break;
    case 'B': btn = B_B; break;
    case 'R': btn = B_RIGHT; break;
    case 'L': btn = B_LEFT; break;
    case 'Y': btn = B_Y; break;
    default: b.plan_n = 0; return 0;
    }
    if (step == 'X' && ctx.ui.target_entry_lock > 0) return 0;
    snapshot(ctx);
    b.phase = 1;
    return btn;
}

// ---------------------------------------------------------------------------- turn log
// One line per rules-visible change. No ticks.
namespace {
char g_last[1024];
int  g_events = 0;

void fmt_state(const bt::BattleCtx& ctx, char* out, int cap) {
    int n = 0;
    n += snprintf(out + n, cap - n, "active=%s turn_i=%d |", ctx.active ? ctx.active->id : "-", ctx.turn_i);
    for (const bt::Actor& a : ctx.actors) {
        n += snprintf(out + n, cap - n, " %s:%d/%d", a.id, a.hp, a.hp_max);
        for (const bt::StatusInst& s : a.status)
            n += snprintf(out + n, cap - n, "%s%d.%d", s.id == bt::StatusId::Burn ? "+burn" : s.id == bt::StatusId::Guard ? "+guard" :
                          s.id == bt::StatusId::Charge ? "+charge" : "+stun", s.turns, s.dot);
        if (n >= cap - 64) break;
    }
}
bt::BattleState g_prev_state = bt::BattleState::Command;
} // namespace

void turnlog_begin(FILE* f) { g_last[0] = 0; g_events = 0; g_prev_state = bt::BattleState::Command; (void)f; }

// Call after every game_tick while the battle runs.
void turnlog_tick(FILE* f) {
    const bt::BattleCtx& ctx = bt::battle_ctx();
    // an action was resolved this tick: Resolve -> Animate
    if (g_prev_state == bt::BattleState::Resolve && ctx.state == bt::BattleState::Animate && ctx.pending.skill) {
        fprintf(f, "ACT %d %s %s ->", ++g_events, ctx.active ? ctx.active->id : "-", ctx.pending.skill->id);
        for (const bt::Actor* t : ctx.pending.targets) fprintf(f, " %s", t ? t->id : "-");
        fprintf(f, "\n");
    }
    g_prev_state = ctx.state;
    char cur[1024];
    fmt_state(ctx, cur, sizeof cur);
    if (strcmp(cur, g_last) != 0) {
        fprintf(f, "ST %s\n", cur);
        snprintf(g_last, sizeof g_last, "%s", cur);
    }
}

void turnlog_end(FILE* f) {
    const bt::BattleCtx& ctx = bt::battle_ctx();
    if (bt::rules().potions) fprintf(f, "POTIONS %d\n", ctx.potions);   // not under --rules legacy (baseline format)
    fprintf(f, "END outcome=%s state=%s\n", ctx.outcome == bt::BattleOutcome::Won ? "won" : ctx.outcome == bt::BattleOutcome::Lost ? "lost" : "none",
            bt::battle_state_name(ctx.state));
}
