// input.h - per-tick button state with edges (replaces ui.lua's "idle baseline" click hack).
// input_update() samples eng::input_down() once per game tick; edges compare to the previous tick.
#pragma once
#include "engine.h"
namespace bt {
void input_update();
void input_reset();                 // forget history (no edges on the next tick)
bool input_held(eng::Key k);
bool input_pressed(eng::Key k);     // went down this tick
bool input_released(eng::Key k);    // went up this tick
}
