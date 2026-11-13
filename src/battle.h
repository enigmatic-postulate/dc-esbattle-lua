// battle.h - port of battle/battle.lua + game/init.lua + boot.lua (the update/draw entry points).
#pragma once
#include "battle_state.h"
namespace bt {
void battle_init();              // module load: assets + M.start()
void battle_start();             // Lua: M.start()
void battle_update(real dt);     // Lua: update(dt)
void battle_draw();              // Lua: draw()
BattleCtx& battle_ctx();         // read access for tools/tests
const char* battle_state_name(BattleState s);
}
