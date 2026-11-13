// skills.h - port of data/skills.lua
#pragma once
#include "battle_state.h"
namespace bt {
enum SkillIndex { SK_ATTACK, SK_FIREBALL, SK_GUARD, SK_ITEMS, SK_COUNT };
const SkillDef& skill(SkillIndex i);         // Lua: skills.by_id[...] / skills.list[...]
const SkillDef* skill_by_id(const char* id); // string lookup for data-driven callers
}
