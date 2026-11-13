// encounters.h - encounter table (was data/encounters/test_slimes.lua). Slice task A4:
// battles are started from data instead of a hard-coded spawn.
#pragma once
#include "battle_state.h"
#include "chars.h"
#include "party.h"
namespace bt {

struct EnemySpec {
    const char* id;
    CharSheet   sheet;
    int         hp;
    Stats       stats;      // str def mdef int agi
    real        x, y;       // formation position (screen)
};

struct EncounterDef {
    const char* id;
    int         count;
    EnemySpec   enemies[4];
};

const EncounterDef* encounter_by_id(const char* id);   // nullptr if unknown
void spawn_encounter(BattleCtx& ctx, const EncounterDef& enc, const PartyState& party);

}
