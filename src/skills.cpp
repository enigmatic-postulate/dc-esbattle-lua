// skills.cpp - port of data/skills.lua
// canon_target()/find_actor_by_id() existed in Lua to re-resolve possibly-copied
// actor tables; targets are Actor* into ctx.actors here, so they are identity.
#include "skills.h"
#include "combat.h"
#include "status.h"
#include "fx.h"
#include "presentation.h"
#include <string.h>

namespace bt {

// ---- ATTACK (single enemy) --------------------------------------------------
static SkillResult attack_resolve(BattleCtx&, Actor& user, const ActorList& targets) {
    SkillResult r;
    (void)user;
    if (targets.empty()) return r;                 // reason = "no_targets"
    Actor* t = targets[0];
    if (!t || t->hp <= 0) return r;                // reason = "target_dead"
    int dmg = damage_physical(&user, t, 0);
    apply_damage(t, dmg);
    t->hp = clampv(t->hp, 0, t->hp_max);
    r.ok = true; r.kind = ResultKind::Single; r.target = t; r.dmg = dmg;
    return r;
}

static void attack_present(BattleCtx& ctx, Actor& user, const SkillResult& res, const ActorList&) {
    if (!res.ok || !res.target) return;
    Actor* t = res.target;
    pres_lunge(ctx, &user, t, T_MOVE, 22);
    pres_hit_flash(ctx, t, T_FLASH_HIT);
    pres_recoil(ctx, t, T_MOVE, 10);
    fx_float_damage(ctx, *t, res.dmg);
    t->hurt_ticks = T_HURT_HIT;
}

// ---- FIREBALL (AOE all enemies) --------------------------------------------
static SkillResult fireball_resolve(BattleCtx&, Actor& user, const ActorList& targets) {
    SkillResult r;
    (void)user;
    for (Actor* t : targets) {
        if (!t || t->hp <= 0) continue;
        int base = damage_magic(&user, t, 0);
        // Lua: math.max(1, math.floor(base * 0.65)). Integer form is exact for all
        // base >= 0; a float port would mis-round (e.g. base=20 -> 12 in float32).
        int dmg = maxv(1, (base * 65) / 100);
        apply_damage(t, dmg);
        t->hp = clampv(t->hp, 0, t->hp_max);
        r.hits.push(Hit{ t, dmg });
    }
    r.ok = true; r.kind = ResultKind::Aoe;
    return r;
}

static void fireball_present(BattleCtx& ctx, Actor&, const SkillResult& res, const ActorList&) {
    if (!res.ok) return;
    for (const Hit& h : res.hits) {
        Actor* t = h.target;
        if (!t) continue;
        pres_hit_flash(ctx, t, T_FLASH_AOE);
        pres_recoil(ctx, t, T_MOVE, 10);
        t->hurt_ticks = T_HURT_AOE;
        fx_float_damage(ctx, *t, h.dmg);
        // LEGACY: a rules mutation (burn) lives in the *presentation* hook, as in Lua.
        status_add(ctx, *t, StatusId::Burn, 2, 2);
    }
}

// ---- GUARD (self) ------------------------------------------------------------
static SkillResult guard_resolve(BattleCtx& ctx, Actor& user, const ActorList&) {
    SkillResult r;
    if (user.hp <= 0) return r;                    // reason = "no_user"
    // LEGACY: 1 turn => ticked away at the end of the guarding actor's OWN turn,
    // before any enemy acts, so Guard currently never reduces damage. See REPORT.md.
    status_add(ctx, user, StatusId::Guard, 1);
    r.ok = true; r.kind = ResultKind::Self; r.target = &user;
    return r;
}

static void guard_present(BattleCtx& ctx, Actor&, const SkillResult& res, const ActorList&) {
    if (!res.ok || !res.target) return;
    pres_hit_flash(ctx, res.target, T_FLASH_GUARD);
    fx_float_status(ctx, *res.target, "GUARD");    // LEGACY: on_apply already floated "GUARD" once
}

// ---- ITEMS (placeholder) -----------------------------------------------------
static SkillResult items_resolve(BattleCtx&, Actor&, const ActorList&) {
    SkillResult r; r.ok = true; r.kind = ResultKind::Noop; return r;
}

static const SkillDef SKILLS[SK_COUNT] = {
    { "attack",   "Attack",   TargetMode::EnemySingle, attack_resolve,   attack_present   },
    { "fireball", "Fireball", TargetMode::EnemyAll,    fireball_resolve, fireball_present },
    { "guard",    "Guard",    TargetMode::Self,        guard_resolve,    guard_present    },
    { "items",    "Items",    TargetMode::None,        items_resolve,    nullptr          },
};

const SkillDef& skill(SkillIndex i) { return SKILLS[i]; }

const SkillDef* skill_by_id(const char* id) {
    for (const SkillDef& s : SKILLS) if (strcmp(s.id, id) == 0) return &s;
    return nullptr;
}

} // namespace bt
