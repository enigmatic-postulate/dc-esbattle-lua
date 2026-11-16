// encounters.cpp - see encounters.h
// "test_slimes" reproduces data/encounters/test_slimes.lua exactly (ids, stats, positions);
// the battle regression goldens run on it.
#include "encounters.h"
#include <string.h>

namespace bt {

//                       str def mdef int agi
static const Stats SLIME = { 4,  1,  1,  0,  1 };
static const Stats GUARD = { 5,  2,  2,  0,  5 };
// The Bone Warden (GDD 12.2 starting values, tuned with the harness bots: tools/battle3d/boss_tune.py,
// tests/battle3d/boss_balance.sh). Tuning pass 1 changed two stats: minion STR 4 -> 3 (their chip
// damage decided every fight) and Soul Rend 12 -> 14 (skills.cpp), so that Guard matters. Result
// over 100 seeds: good play wins 95 % (9 HP left, 15 hero turns), naive play 48 %, no-guard play
// 78 % and mostly to Soul Rend - every GDD 12.6 target.
static const Stats WARDEN = { 6,  1,  3,  6,  3 };
static const Stats MINION = { 3,  1,  1,  0,  1 };

// Looks (3D only, GDD 11): the road patrol is a mixed squad (Warrior, Rogue, Mage skeletons; slice
// 3D-4 decision D2), the north-east patrol two Warriors. Rules data (ids, HP, stats) is unchanged.
//                 id          sheet        HP  stats   x    y    look          role         dormant
static const EncounterDef ENCOUNTERS[] = {
    { "test_slimes", 3, { { "slime_a",  SHEET_SLIME, 12, SLIME,  460, 200, LOOK_WARRIOR, ROLE_NONE,   false },
                          { "slime_b",  SHEET_SLIME, 12, SLIME,  500, 260, LOOK_ROGUE,   ROLE_NONE,   false },
                          { "slime_c",  SHEET_SLIME, 12, SLIME,  460, 320, LOOK_MAGE,    ROLE_NONE,   false },
                          { nullptr,    SHEET_SLIME,  0, SLIME,    0,   0, LOOK_AUTO,    ROLE_NONE,   false } },
      nullptr, 0, 0 },
    { "slimes_2",    2, { { "slime_a",  SHEET_SLIME, 12, SLIME,  470, 220, LOOK_WARRIOR, ROLE_NONE,   false },
                          { "slime_b",  SHEET_SLIME, 12, SLIME,  480, 300, LOOK_WARRIOR, ROLE_NONE,   false },
                          { nullptr,    SHEET_SLIME,  0, SLIME,    0,   0, LOOK_AUTO,    ROLE_NONE,   false },
                          { nullptr,    SHEET_SLIME,  0, SLIME,    0,   0, LOOK_AUTO,    ROLE_NONE,   false } },
      nullptr, 0, 0 },
    { "guard_duel",  1, { { "guard",    SHEET_GUARD, 18, GUARD,  470, 250, LOOK_MAGE,    ROLE_NONE,   false },
                          { nullptr,    SHEET_SLIME,  0, SLIME,    0,   0, LOOK_AUTO,    ROLE_NONE,   false },
                          { nullptr,    SHEET_SLIME,  0, SLIME,    0,   0, LOOK_AUTO,    ROLE_NONE,   false },
                          { nullptr,    SHEET_SLIME,  0, SLIME,    0,   0, LOOK_AUTO,    ROLE_NONE,   false } },
      nullptr, 0, 0 },
    // The Bone Warden (GDD 12): actor order = screen order left to right (the target cursor), so
    // the two minion slots flank him; they start dormant and rise with Raise Dead. Turn order by
    // agility: hero, Warden, minions ("acts after the hero, before his minions").
    { "warden",      3, { { "minion_a", SHEET_SLIME,  7, MINION, 430, 220, LOOK_MINION,  ROLE_MINION, true  },
                          { "warden",   SHEET_GUARD, 44, WARDEN, 490, 250, LOOK_WARDEN,  ROLE_BOSS,   false },
                          { "minion_b", SHEET_SLIME,  7, MINION, 430, 300, LOOK_MINION,  ROLE_MINION, true  },
                          { nullptr,    SHEET_SLIME,  0, SLIME,    0,   0, LOOK_AUTO,    ROLE_NONE,   false } },
      "barrow", 30, 3 },
};

const char* encounter_arena(const EncounterDef& e) { return e.arena ? e.arena : "village"; }

PartyState party_default() {
    PartyState p;
    p.hero_hp = 20; p.hero_hp_max = 20;
    p.hero_stats = Stats{ 6, 2, 2, 6, 99 };
    p.potions = 0;
    return p;
}

const EncounterDef* encounter_by_id(const char* id) {
    for (const EncounterDef& e : ENCOUNTERS) if (strcmp(e.id, id) == 0) return &e;
    return nullptr;
}

static Actor make(const char* id, Team team, int hp, int hp_max, Stats st, real x, real y, CharSheet sheet, Dir facing, uint8_t role = ROLE_NONE) {
    Actor a;
    a.id = id; a.team = team; a.hp = hp; a.hp_max = hp_max; a.stats = st;
    a.status.clear();
    a.x = x; a.y = y; a.w = 64; a.h = 64;
    a.sheet = sheet; a.facing = facing; a.hurt_ticks = 0;
    a.role = role;
    return a;
}

void spawn_encounter(BattleCtx& ctx, const EncounterDef& enc, const PartyState& party) {
    ctx.actors.clear();
    ctx.actors.push(make("hero", Team::Player, party.hero_hp, party.hero_hp_max, party.hero_stats,
                         180, 240, SHEET_HERO, DIR_RIGHT));
    for (int i = 0; i < enc.count; ++i) {
        const EnemySpec& e = enc.enemies[i];
        ctx.actors.push(make(e.id, Team::Enemy, e.dormant ? 0 : e.hp, e.hp, e.stats, e.x, e.y, e.sheet, DIR_LEFT, e.role));
    }
}

} // namespace bt
