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
// Uses up this tick's press of k: input_pressed(k) is false for the rest of the tick (slice 3D-5,
// GDD 4: the A that closes a dialog never also starts something else on the same tick).
void input_consume(eng::Key k);
int  input_stick(eng::Axis a);      // analog stick this tick, -128..127 (sampled with the buttons)
// Slice 3D-6 (game flow): while a transition runs the game reads no input at all (held, pressed and the stick read
// as nothing); input_reset() after it makes the next action need a fresh press (GDD 4).
void input_set_lock(bool on);
bool input_locked();
// The next input_update()s use these buttons (bit k = eng::Key k) and stick instead of the pad, until turned off:
// a cutscene skip's fast-forward, the attract demo.
void input_feed(bool on, uint32_t buttons, int ax, int ay);
bool input_fed();
bool input_pad_any();               // any pad button down right now (the attract demo's exit), whatever is fed
}
