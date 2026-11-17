// input.cpp - see input.h
#include "input.h"

namespace bt {

static uint32_t g_cur = 0, g_prev = 0;
static int g_axis[eng::AXIS_COUNT] = { 0, 0 };

void input_update() {
    g_prev = g_cur;
    g_cur = 0;
    for (int k = 0; k < eng::KEY_COUNT; ++k)
        if (eng::input_down((eng::Key)k)) g_cur |= 1u << k;
    for (int a = 0; a < eng::AXIS_COUNT; ++a) {
        int v = eng::input_axis((eng::Axis)a);
        g_axis[a] = v < -128 ? -128 : (v > 127 ? 127 : v);
    }
}

int input_stick(eng::Axis a) { return (a >= 0 && a < eng::AXIS_COUNT) ? g_axis[a] : 0; }

void input_reset() { g_prev = g_cur; }

bool input_held(eng::Key k)     { return (g_cur >> k) & 1u; }
bool input_pressed(eng::Key k)  { return ((g_cur & ~g_prev) >> k) & 1u; }
bool input_released(eng::Key k) { return ((~g_cur & g_prev) >> k) & 1u; }
void input_consume(eng::Key k) { g_prev |= g_cur & (1u << k); }   // no edge left for k this tick

} // namespace bt
