// combat.h - port of battle/combat.lua (damage math + HP mutation).
#pragma once
#include "battle_state.h"
namespace bt {
void apply_damage(Actor* target, int dmg);
int  damage_physical(const Actor* user, const Actor* target, int bonus);
int  damage_magic(const Actor* user, const Actor* target, int bonus);
}
