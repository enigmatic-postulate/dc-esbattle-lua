// encounters.h - encounter table (was data/encounters/test_slimes.lua). Slice task A4:
// battles are started from data instead of a hard-coded spawn.
#pragma once
#include "battle_state.h"
#include "chars.h"
#include "party.h"
namespace bt {

// 3D look of an enemy (slice 3D-4): which model, weapons and sequences it uses. Presentation only;
// LOOK_AUTO picks from the sprite sheet (slime -> Skeleton Warrior, guard -> Skeleton Mage).
enum EnemyLook : uint8_t { LOOK_AUTO, LOOK_WARRIOR, LOOK_ROGUE, LOOK_MAGE, LOOK_WARDEN, LOOK_MINION, LOOK_COUNT };

struct EnemySpec {
    const char* id;
    CharSheet   sheet;
    int         hp;
    Stats       stats;      // str def mdef int agi
    real        x, y;       // formation position (screen, sprite battle)
    EnemyLook   look;       // 3D model (presentation only)
    uint8_t     role;       // ROLE_NONE / ROLE_BOSS / ROLE_MINION (the boss AI)
    bool        dormant;    // starts at 0 HP: a slot the boss raises (Raise Dead)
};

struct EncounterDef {
    const char* id;
    int         count;
    EnemySpec   enemies[4];
    const char* arena;       // 3D arena (nullptr: "village")
    // --battle-test only (slice 3D-5): the "boss-ready" party the balance tests use (max HP 30, fully healed,
    // 3 potions). In the game these come from the quest (Wren's blessing, Brom's potions; Progress).
    int         test_max_hp; // > 0: max HP of the battle-test hero
    int         test_potions;
};

const EncounterDef* encounter_by_id(const char* id);   // nullptr if unknown
const char* encounter_arena(const EncounterDef& enc);  // arena the 3D battle uses ("village")
void spawn_encounter(BattleCtx& ctx, const EncounterDef& enc, const PartyState& party);

}
