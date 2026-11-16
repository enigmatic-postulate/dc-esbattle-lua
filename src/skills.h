// skills.h - port of data/skills.lua
#pragma once
#include "battle_state.h"
namespace bt {
// SK_POTION was the unused "Items" placeholder (GDD 11.1). The SK_BONE_SLASH.. entries are the Bone
// Warden's (GDD 12.3); SK_PHASE is his phase-change turn (minions crumble, two rise).
enum SkillIndex { SK_ATTACK, SK_FIREBALL, SK_GUARD, SK_POTION,
                  SK_BONE_SLASH, SK_RAISE_DEAD, SK_CHARGE, SK_SOUL_REND, SK_GRAVE_FIRE, SK_BONE_WARD, SK_PHASE,
                  SK_COUNT };
const int POTION_HEAL = 12;
// Soul Rend's power: damage_physical(user, target, bonus) = STR + bonus - DEF, halved by Guard.
// A skill stat (GDD 12.6 tunes stats, never formulas); boss_balance.sh sweeps it (--set rend.bonus=N).
extern int g_soul_rend_bonus;
const SkillDef& skill(SkillIndex i);         // Lua: skills.by_id[...] / skills.list[...]
const SkillDef* skill_by_id(const char* id); // string lookup for data-driven callers
int skill_index(const SkillDef* s);          // SkillIndex of a SKILLS[] entry, -1 if none
}
