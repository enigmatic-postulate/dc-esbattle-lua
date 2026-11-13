// party.h - state that persists between field and battles.
#pragma once
#include "battle_state.h"
namespace bt {
struct PartyState {
    int   hero_hp, hero_hp_max;
    Stats hero_stats;
};
PartyState party_default();   // fresh hero (used at boot and after Game Over)
}
