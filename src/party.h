// party.h - state that persists between field and battles.
#pragma once
#include "battle_state.h"
namespace bt {
// Skills learned (GDD 11.1; slice 3D-5): a bit set, copied into the battle (BattleCtx.skills).
const uint32_t SKILL_FIRE = 1u;
const uint32_t SKILLS_ALL = SKILL_FIRE;

struct PartyState {
    int   hero_hp, hero_hp_max;
    Stats hero_stats;
    int   potions;            // GDD 11.1: from Progress (Brom's potions) at battle_begin, back to it after
    uint32_t skills;          // SKILL_*: from Progress (Wren teaches Fire) at battle_begin
};
PartyState party_default();   // fresh hero: 20 HP, no potions, every skill (battle tests; the game sets them from Progress)
}
