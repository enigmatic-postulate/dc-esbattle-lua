// input_probe.h - port of battle/input_probe.lua (button reads + on-screen button debug strip).
#pragma once
#include "battle_state.h"
namespace bt {
void probe_init(BattleCtx& ctx);
bool probe_down(eng::Key k);
void probe_draw(BattleCtx& ctx);
}
