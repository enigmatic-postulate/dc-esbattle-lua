// input_probe.cpp - port of battle/input_probe.lua
// probe.pressed()/reset_edges()/update() were no-ops or unused by callers and are not ported.
#include "input_probe.h"
#include "assets.h"

namespace bt {

void probe_init(BattleCtx& ctx) { ctx.ui.probe_spr = asset_sprite("/rd/plasma.png"); }

bool probe_down(eng::Key k) { return eng::input_down(k); }

void probe_draw(BattleCtx& ctx) {
    static const eng::Key CAND[] = { eng::KEY_FIRE, eng::KEY_LEFT, eng::KEY_RIGHT,
                                     eng::KEY_UP, eng::KEY_DOWN, eng::KEY_START };
    const eng::Sprite spr = ctx.ui.probe_spr;
    if (!spr) return;
    const real x0 = 40, y0 = 450, dx = 26;
    for (int i = 0; i < 6; ++i) {
        real s = probe_down(CAND[i]) ? (real)1.8 : (real)1.0;
        real x = x0 + i * dx;
        eng::sprite_draw(spr, (float)x, (float)y0, (float)(10 * s), (float)(10 * s), 0);
    }
    eng::sprite_draw(spr, (float)x0, (float)(y0 - 18), 18, 18, 0);   // marker above "fire"
}

} // namespace bt
