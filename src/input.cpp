// input.cpp - see input.h
#include "input.h"

namespace bt {

static uint32_t g_cur = 0, g_prev = 0;
static int g_axis[eng::AXIS_COUNT] = { 0, 0 };
static bool g_locked = false;
static bool g_fed = false;
static uint32_t g_feed_b = 0;
static int g_feed_ax = 0, g_feed_ay = 0;

void input_update() {
    g_prev = g_cur;
    g_cur = 0;
    if (g_fed) {                                       // slice 3D-6: a cutscene skip, the attract demo
        g_cur = g_feed_b;
        g_axis[eng::AXIS_X] = g_feed_ax; g_axis[eng::AXIS_Y] = g_feed_ay;
        return;
    }
    for (int k = 0; k < eng::KEY_COUNT; ++k)
        if (eng::input_down((eng::Key)k)) g_cur |= 1u << k;
    for (int a = 0; a < eng::AXIS_COUNT; ++a) {
        int v = eng::input_axis((eng::Axis)a);
        g_axis[a] = v < -128 ? -128 : (v > 127 ? 127 : v);
    }
}

int input_stick(eng::Axis a) { return (!g_locked && a >= 0 && a < eng::AXIS_COUNT) ? g_axis[a] : 0; }

void input_reset() { g_prev = g_cur; }

bool input_held(eng::Key k)     { return !g_locked && ((g_cur >> k) & 1u); }
bool input_pressed(eng::Key k)  { return !g_locked && (((g_cur & ~g_prev) >> k) & 1u); }
bool input_released(eng::Key k) { return !g_locked && (((~g_cur & g_prev) >> k) & 1u); }
void input_consume(eng::Key k) { g_prev |= g_cur & (1u << k); }   // no edge left for k this tick

void input_set_lock(bool on) { g_locked = on; }
bool input_locked() { return g_locked; }
void input_feed(bool on, uint32_t buttons, int ax, int ay) { g_fed = on; g_feed_b = buttons; g_feed_ax = ax; g_feed_ay = ay; }
bool input_fed() { return g_fed; }
bool input_pad_any() {
    for (int k = 0; k < eng::KEY_COUNT; ++k) if (eng::input_down((eng::Key)k)) return true;
    return false;
}

} // namespace bt
