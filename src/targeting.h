// targeting.h - port of battle/targeting.lua. Lists hold Actor* (references), never copies.
#pragma once
#include "battle_state.h"
namespace bt {
ActorList alive_team(BattleCtx& ctx, Team team);
ActorList alive_enemies(BattleCtx& ctx, const Actor& user);
ActorList alive_allies(BattleCtx& ctx, const Actor& user);
int       count_alive_team(const BattleCtx& ctx, Team team);
// cursor_index is 0-based and only used for *_single modes (clamped into range).
ActorList select_targets(BattleCtx& ctx, Actor& user, TargetMode mode, int cursor_index);
}
