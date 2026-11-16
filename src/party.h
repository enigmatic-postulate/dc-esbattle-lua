// party.h - state that persists between field and battles.
#pragma once
#include "battle_state.h"
namespace bt {
struct PartyState {
    int   hero_hp, hero_hp_max;
    Stats hero_stats;
    int   potions;            // GDD 11.1 (Brom's potions; the slice has no Brom yet: the boss's waystone gives 3)
};
PartyState party_default();   // fresh hero (used at boot and after Game Over)
}
