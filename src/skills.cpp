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
    // LEGACY (rules().guard_fix off): 1 turn => ticked away at the end of the guarding actor's OWN
    // turn, before any enemy acts, so Guard never reduced damage (REPORT.md). The fix: 2 turns, so it
    // survives the end of this turn; status.cpp removes it when the actor's next turn starts.
    status_add(ctx, user, StatusId::Guard, rules().guard_fix ? 2 : 1);
    r.ok = true; r.kind = ResultKind::Self; r.target = &user;
    return r;
}

static void guard_present(BattleCtx& ctx, Actor&, const SkillResult& res, const ActorList&) {
    if (!res.ok || !res.target) return;
    pres_hit_flash(ctx, res.target, T_FLASH_GUARD);
    fx_float_status(ctx, *res.target, "GUARD");    // LEGACY: on_apply already floated "GUARD" once
}

// ---- POTION (self; GDD 11.1: the former "Items" placeholder) ------------------
static SkillResult potion_resolve(BattleCtx& ctx, Actor& user, const ActorList&) {
    SkillResult r;
    if (user.hp <= 0 || ctx.potions <= 0) return r;
    const int before = user.hp;
    user.hp = minv(user.hp_max, user.hp + POTION_HEAL);
    ctx.potions -= 1;
    r.ok = true; r.kind = ResultKind::Self; r.target = &user; r.dmg = -(user.hp - before);   // negative: healed
    return r;
}

// ---- the Bone Warden (GDD 12.3) ------------------------------------------------------
int g_soul_rend_bonus = 10;     // STR 6 + 10 - DEF 2 = 14 (7 when guarding). GDD 12.3 started at 8 (12 / 6);
                                // tuned with the balance bots (encounters.cpp, WARDEN)
static Actor* only_target(const ActorList& targets) { return targets.empty() ? nullptr : targets[0]; }

static SkillResult physical_hit(Actor& user, Actor* t, int bonus) {
    SkillResult r;
    if (!t || t->hp <= 0) return r;
    const int dmg = damage_physical(&user, t, bonus);
    apply_damage(t, dmg);
    t->hp = clampv(t->hp, 0, t->hp_max);
    r.ok = true; r.kind = ResultKind::Single; r.target = t; r.dmg = dmg;
    return r;
}

// Bone Slash: the staff, physical (STR 6 - DEF 2 = 4; 2 when guarding)
static SkillResult bone_slash_resolve(BattleCtx&, Actor& user, const ActorList& targets) {
    return physical_hit(user, only_target(targets), 0);
}

// Soul Rend: only after CHARGE (the AI guarantees it); 12, halved by Guard. Consumes the charge.
static SkillResult soul_rend_resolve(BattleCtx&, Actor& user, const ActorList& targets) {
    status_remove(user, StatusId::Charge);
    return physical_hit(user, only_target(targets), g_soul_rend_bonus);
}

// CHARGE: the telegraph. No damage; the Warden is charging until Soul Rend spends it.
static SkillResult charge_resolve(BattleCtx& ctx, Actor& user, const ActorList&) {
    SkillResult r;
    if (user.hp <= 0) return r;
    status_remove(user, StatusId::Charge);
    status_add(ctx, user, StatusId::Charge, 2);
    r.ok = true; r.kind = ResultKind::Self; r.target = &user;
    return r;
}

// Bone Ward: Guard on himself (halves physical damage until his next turn; the guard fix rule)
static SkillResult bone_ward_resolve(BattleCtx& ctx, Actor& user, const ActorList&) {
    SkillResult r;
    if (user.hp <= 0) return r;
    status_add(ctx, user, StatusId::Guard, 2);
    r.ok = true; r.kind = ResultKind::Self; r.target = &user;
    return r;
}

// Grave Fire: magic on every foe, 65 % like Fireball, and Burn 2 x 2 turns that does not stack past
// one instance (a second Grave Fire renews it)
static SkillResult grave_fire_resolve(BattleCtx& ctx, Actor& user, const ActorList& targets) {
    SkillResult r;
    for (Actor* t : targets) {
        if (!t || t->hp <= 0) continue;
        const int dmg = maxv(1, (damage_magic(&user, t, 0) * 65) / 100);
        apply_damage(t, dmg);
        t->hp = clampv(t->hp, 0, t->hp_max);
        r.hits.push(Hit{ t, dmg });
        if (t->hp > 0) {
            status_remove(*t, StatusId::Burn);
            status_add(ctx, *t, StatusId::Burn, 2, 2);
        }
    }
    r.ok = true; r.kind = ResultKind::Aoe;
    return r;
}

// Raise Dead: every fallen minion rises at full HP (at most two stand: there are two slots)
static SkillResult raise_dead_resolve(BattleCtx& ctx, Actor& user, const ActorList&) {
    SkillResult r;
    if (user.hp <= 0) return r;
    for (Actor& a : ctx.actors) {
        if (a.role != ROLE_MINION || a.hp > 0) continue;
        a.hp = a.hp_max;
        a.status.clear();
        r.hits.push(Hit{ &a, 0 });
    }
    r.ok = true; r.kind = ResultKind::Summon;
    return r;
}

// The phase change (his first turn at <= 50 % HP): the standing minions crumble, two fresh ones rise.
// hits: every minion slot, dmg = 1 if it crumbled first.
static SkillResult phase_resolve(BattleCtx& ctx, Actor& user, const ActorList&) {
    SkillResult r;
    if (user.hp <= 0) return r;
    for (Actor& a : ctx.actors) {
        if (a.role != ROLE_MINION) continue;
        const bool stood = a.hp > 0;
        a.hp = a.hp_max;
        a.status.clear();
        r.hits.push(Hit{ &a, stood ? 1 : 0 });
    }
    r.ok = true; r.kind = ResultKind::Summon;
    return r;
}

static const SkillDef SKILLS[SK_COUNT] = {
    { "attack",     "Attack",     TargetMode::EnemySingle, attack_resolve,     attack_present   },
    { "fireball",   "Fireball",   TargetMode::EnemyAll,    fireball_resolve,   fireball_present },
    { "guard",      "Guard",      TargetMode::Self,        guard_resolve,      guard_present    },
    { "potion",     "Potion",     TargetMode::Self,        potion_resolve,     nullptr          },
    { "bone_slash", "Bone Slash", TargetMode::EnemySingle, bone_slash_resolve, nullptr          },
    { "raise_dead", "Raise Dead", TargetMode::Self,        raise_dead_resolve, nullptr          },
    { "charge",     "Charge",     TargetMode::Self,        charge_resolve,     nullptr          },
    { "soul_rend",  "Soul Rend",  TargetMode::EnemySingle, soul_rend_resolve,  nullptr          },
    { "grave_fire", "Grave Fire", TargetMode::EnemyAll,    grave_fire_resolve, nullptr          },
    { "bone_ward",  "Bone Ward",  TargetMode::Self,        bone_ward_resolve,  nullptr          },
    { "phase",      "Phase",      TargetMode::Self,        phase_resolve,      nullptr          },
};

const SkillDef& skill(SkillIndex i) { return SKILLS[i]; }

int skill_index(const SkillDef* s) {
    for (int i = 0; i < SK_COUNT; ++i) if (s == &SKILLS[i]) return i;
    return -1;
}

const SkillDef* skill_by_id(const char* id) {
    for (const SkillDef& s : SKILLS) if (strcmp(s.id, id) == 0) return &s;
    return nullptr;
}

} // namespace bt
