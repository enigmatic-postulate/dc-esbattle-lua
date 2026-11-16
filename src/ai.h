// ai.h - enemy decisions. Every enemy but the boss keeps the original rule (Attack the first living
// opponent); the Bone Warden follows GDD 12.4 / Appendix C: a fixed rotation per phase with
// overrides, no randomness, so a given sequence of player commands always gives the same fight.
#pragma once
#include "battle_state.h"
namespace bt {
struct EnemyChoice { int skill; ActorList targets; };
EnemyChoice enemy_choose(BattleCtx& ctx, Actor& actor);
void boss_begin(BattleCtx& ctx);      // finds the boss (role ROLE_BOSS), resets his state
// After every rules step: a fallen boss takes his minions with him (pushes a Crumble event).
void boss_after(BattleCtx& ctx);
int  boss_alive_minions(const BattleCtx& ctx);
}
