// encounters.cpp - port of data/encounters/test_slimes.lua
// Stats are fully explicit: Lua fell back to mdef := def and int := 0 when absent.
#include "encounters.h"
#include "assets.h"

namespace bt {

static Actor make(const char* id, Team team, int hp, Stats st, real x, real y, eng::Sprite spr) {
    Actor a;
    a.id = id; a.team = team; a.hp = hp; a.hp_max = hp; a.stats = st;
    a.status.clear();
    a.x = x; a.y = y; a.w = 64; a.h = 64; a.spr = spr; a.hurt_t = 0;
    return a;
}

void spawn_test_slimes(BattleCtx& ctx) {
    eng::Sprite spr = asset_sprite("/rd/es_sprite_64.png");
    //                           str def mdef int agi
    const Stats HERO  = {          6,  2,   2,  6, 99 };
    const Stats SLIME = {          4,  1,   1,  0,  1 };
    ctx.actors.clear();
    ctx.actors.push(make("hero",    Team::Player, 20, HERO,  180, 240, spr));
    ctx.actors.push(make("slime_a", Team::Enemy,  12, SLIME, 460, 200, spr));
    ctx.actors.push(make("slime_b", Team::Enemy,  12, SLIME, 500, 260, spr));
    ctx.actors.push(make("slime_c", Team::Enemy,  12, SLIME, 460, 320, spr));
}

} // namespace bt
