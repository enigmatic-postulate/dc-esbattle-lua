// encounters.cpp - see encounters.h
// "test_slimes" reproduces data/encounters/test_slimes.lua exactly (ids, stats, positions);
// the battle regression goldens run on it.
#include "encounters.h"
#include <string.h>

namespace bt {

//                       str def mdef int agi
static const Stats SLIME = { 4,  1,  1,  0,  1 };
static const Stats GUARD = { 5,  2,  2,  0,  5 };

static const EncounterDef ENCOUNTERS[] = {
    { "test_slimes", 3, { { "slime_a", SHEET_SLIME, 12, SLIME, 460, 200 },
                          { "slime_b", SHEET_SLIME, 12, SLIME, 500, 260 },
                          { "slime_c", SHEET_SLIME, 12, SLIME, 460, 320 } } },
    { "slimes_2",    2, { { "slime_a", SHEET_SLIME, 12, SLIME, 470, 220 },
                          { "slime_b", SHEET_SLIME, 12, SLIME, 480, 300 } } },
    { "guard_duel",  1, { { "guard",   SHEET_GUARD, 18, GUARD, 470, 250 } } },
};

PartyState party_default() {
    PartyState p;
    p.hero_hp = 20; p.hero_hp_max = 20;
    p.hero_stats = Stats{ 6, 2, 2, 6, 99 };
    return p;
}

const EncounterDef* encounter_by_id(const char* id) {
    for (const EncounterDef& e : ENCOUNTERS) if (strcmp(e.id, id) == 0) return &e;
    return nullptr;
}

static Actor make(const char* id, Team team, int hp, int hp_max, Stats st, real x, real y, CharSheet sheet, Dir facing) {
    Actor a;
    a.id = id; a.team = team; a.hp = hp; a.hp_max = hp_max; a.stats = st;
    a.status.clear();
    a.x = x; a.y = y; a.w = 64; a.h = 64;
    a.sheet = sheet; a.facing = facing; a.hurt_ticks = 0;
    return a;
}

void spawn_encounter(BattleCtx& ctx, const EncounterDef& enc, const PartyState& party) {
    ctx.actors.clear();
    ctx.actors.push(make("hero", Team::Player, party.hero_hp, party.hero_hp_max, party.hero_stats,
                         180, 240, SHEET_HERO, DIR_RIGHT));
    for (int i = 0; i < enc.count; ++i) {
        const EnemySpec& e = enc.enemies[i];
        ctx.actors.push(make(e.id, Team::Enemy, e.hp, e.hp, e.stats, e.x, e.y, e.sheet, DIR_LEFT));
    }
}

} // namespace bt
