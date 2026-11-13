// input.cpp - see input.h
#include "input.h"

namespace bt {

static uint32_t g_cur = 0, g_prev = 0;

void input_update() {
    g_prev = g_cur;
    g_cur = 0;
    for (int k = 0; k < eng::KEY_COUNT; ++k)
        if (eng::input_down((eng::Key)k)) g_cur |= 1u << k;
}

void input_reset() { g_prev = g_cur; }

bool input_held(eng::Key k)     { return (g_cur >> k) & 1u; }
bool input_pressed(eng::Key k)  { return ((g_cur & ~g_prev) >> k) & 1u; }
bool input_released(eng::Key k) { return ((~g_cur & g_prev) >> k) & 1u; }

} // namespace bt
