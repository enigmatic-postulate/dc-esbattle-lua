// combat.cpp - port of battle/combat.lua
//
// combat.has_status() duplicated status.has() in Lua only to dodge a circular
// require(); C++ has no such problem so it calls status_has() directly.
// combat.add_status() was never called and is not ported.
#include "combat.h"
#include "status.h"

namespace bt {

void apply_damage(Actor* target, int dmg) {
    if (!target || target->hp <= 0) return;
    target->hp = maxv(0, target->hp - maxv(0, dmg));
}

int damage_physical(const Actor* user, const Actor* target, int bonus) {
    if (!user || !target) return 0;
    int dmg = maxv(1, (user->stats.str + bonus) - target->stats.def);
    // Guard halves incoming physical damage (rounded up). Integer math == Lua floor((d+1)/2) for d >= 1.
    if (status_has(*target, StatusId::Guard)) dmg = maxv(1, (dmg + 1) / 2);
    return dmg;
}

int damage_magic(const Actor* user, const Actor* target, int bonus) {
    if (!user || !target) return 0;
    return maxv(1, (user->stats.intel + bonus) - target->stats.mdef);   // guard does not reduce magic
}

} // namespace bt
