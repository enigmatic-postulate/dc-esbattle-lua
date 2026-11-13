// input_probe.cpp - port of battle/input_probe.lua (button reads + on-screen debug strip).
// probe.pressed()/reset_edges()/update() were no-ops or unused by callers and are not ported.
#include "input_probe.h"
#include "icons.h"

namespace bt {

void probe_init(BattleCtx&) {}

bool probe_down(eng::Key k) { return eng::input_down(k); }

void probe_draw(BattleCtx&) {
    static const eng::Key CAND[] = { eng::KEY_FIRE, eng::KEY_LEFT, eng::KEY_RIGHT,
                                     eng::KEY_UP, eng::KEY_DOWN, eng::KEY_START };
    static const Icon GLYPH[] = { ICON_PAD_A, ICON_PAD_LEFT, ICON_PAD_RIGHT,
                                  ICON_PAD_UP, ICON_PAD_DOWN, ICON_PAD_START };
    const real x0 = 40, y0 = 450, dx = 26;
    for (int i = 0; i < 6; ++i)
        icon_draw(GLYPH[i], x0 + i * dx, y0, probe_down(CAND[i]) ? ICON_2X : ICON_1X);
}

} // namespace bt
