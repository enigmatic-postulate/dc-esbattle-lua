// ai.cpp - see ai.h
#include "ai.h"
#include "skills.h"
#include "status.h"
#include "targeting.h"

namespace bt {

// GDD Appendix C. Phase 1 (HP 44-23) and phase 2 (22-0) rotations, one entry per Warden turn.
static const int P1[] = { SK_BONE_SLASH, SK_RAISE_DEAD, SK_BONE_SLASH, SK_CHARGE, SK_SOUL_REND };
static const int P2[] = { SK_GRAVE_FIRE, SK_BONE_SLASH, SK_BONE_WARD, SK_CHARGE, SK_SOUL_REND, SK_RAISE_DEAD };
static const int RAISE_COOLDOWN = 3;      // Warden turns

int boss_alive_minions(const BattleCtx& ctx) {
    int n = 0;
    for (const Actor& a : ctx.actors) if (a.role == ROLE_MINION && a.hp > 0) ++n;
    return n;
}

void boss_begin(BattleCtx& ctx) {
    ctx.boss = BossState();
    for (Actor& a : ctx.actors) if (a.role == ROLE_BOSS) ctx.boss.boss = &a;
}

static EnemyChoice targets_for(BattleCtx& ctx, Actor& user, int sk) {
    EnemyChoice c;
    c.skill = sk;
    const SkillDef& d = skill((SkillIndex)sk);
    c.targets = select_targets(ctx, user, d.target_mode, 0);
    return c;
}

static EnemyChoice boss_choose(BattleCtx& ctx, Actor& w) {
    BossState& B = ctx.boss;
    if (B.raise_cd > 0) --B.raise_cd;
    // the transition: his first turn at or below half HP (once)
    if (!B.transition_done && w.hp * 2 <= w.hp_max) {
        B.transition_done = true;
        B.phase = 2;
        B.rot_i = 0;
        B.raise_cd = 0;
        B.last_skill = SK_PHASE;
        return targets_for(ctx, w, SK_PHASE);
    }
    const int* rot = B.phase == 1 ? P1 : P2;
    const int n = B.phase == 1 ? (int)(sizeof P1 / sizeof P1[0]) : (int)(sizeof P2 / sizeof P2[0]);
    int sk = SK_BONE_SLASH;
    for (int tries = 0; tries < n; ++tries) {
        sk = rot[B.rot_i];
        B.rot_i = (B.rot_i + 1) % n;
        if (sk == SK_RAISE_DEAD && (boss_alive_minions(ctx) >= 2 || B.raise_cd > 0)) continue;   // next entry instead
        if (sk == SK_SOUL_REND && !status_has(w, StatusId::Charge)) continue;                    // only after CHARGE
        if (sk == SK_BONE_WARD && B.last_skill == SK_BONE_WARD) continue;                        // never twice in a row
        break;
    }
    if (sk == SK_RAISE_DEAD) B.raise_cd = RAISE_COOLDOWN;
    B.last_skill = sk;
    return targets_for(ctx, w, sk);
}

EnemyChoice enemy_choose(BattleCtx& ctx, Actor& actor) {
    if (actor.role == ROLE_BOSS && ctx.boss.boss == &actor) return boss_choose(ctx, actor);
    // every other enemy: basic attack on the first alive opponent (the original rule)
    EnemyChoice c;
    c.skill = SK_ATTACK;
    c.targets = select_targets(ctx, actor, skill(SK_ATTACK).target_mode, 0);
    return c;
}

void boss_after(BattleCtx& ctx) {
    Actor* b = ctx.boss.boss;
    if (!b || b->hp > 0) return;
    BattleEvent ev;
    ev.kind = EvKind::Crumble; ev.user = b; ev.skill = -1; ev.result = ResultKind::Noop; ev.dmg = 0;
    for (Actor& a : ctx.actors) {
        if (a.role != ROLE_MINION || a.hp <= 0) continue;
        a.hp = 0;
        a.status.clear();
        ev.hits.push(Hit{ &a, 0 });
    }
    if (!ev.hits.empty()) ctx.events.push(ev);
}

}
