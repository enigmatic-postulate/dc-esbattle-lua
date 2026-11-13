// targeting.cpp - port of battle/targeting.lua
#include "targeting.h"

namespace bt {

static Team other_team(Team t) { return t == Team::Player ? Team::Enemy : Team::Player; }

ActorList alive_team(BattleCtx& ctx, Team team) {
    ActorList out;
    for (Actor& a : ctx.actors)
        if (alive(&a) && a.team == team) out.push(&a);
    return out;
}

ActorList alive_enemies(BattleCtx& ctx, const Actor& user) { return alive_team(ctx, other_team(user.team)); }
ActorList alive_allies(BattleCtx& ctx, const Actor& user)  { return alive_team(ctx, user.team); }

int count_alive_team(const BattleCtx& ctx, Team team) {
    int n = 0;
    for (const Actor& a : ctx.actors)
        if (alive(&a) && a.team == team) ++n;
    return n;
}

ActorList select_targets(BattleCtx& ctx, Actor& user, TargetMode mode, int cursor_index) {
    ActorList out;
    switch (mode) {
    case TargetMode::Self:      out.push(&user); return out;
    case TargetMode::EnemyAll:  return alive_enemies(ctx, user);
    case TargetMode::AllyAll:   return alive_allies(ctx, user);
    case TargetMode::EnemySingle: {
        ActorList list = alive_enemies(ctx, user);
        if (list.empty()) return out;
        int idx = clampv(cursor_index, 0, list.size() - 1);
        out.push(list[idx]);
        return out;
    }
    case TargetMode::None:
    default:                    return out;   // unknown mode => nothing
    }
}

} // namespace bt
