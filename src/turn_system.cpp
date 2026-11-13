// turn_system.cpp - port of battle/turn_system.lua
#include "turn_system.h"
#include "status.h"

namespace bt {

// Lua used table.sort (agi descending). table.sort is NOT stable: with the test
// encounter it produced hero, slime_b, slime_a, slime_c. The port defines the
// tie-break explicitly: equal agi keeps encounter order (stable insertion sort,
// no allocation). This is a documented, intentional divergence.
void build_order(BattleCtx& ctx) {
    ctx.order.clear();
    for (Actor& a : ctx.actors) ctx.order.push(&a);
    for (int i = 1; i < ctx.order.size(); ++i) {
        Actor* v = ctx.order[i];
        int j = i - 1;
        while (j >= 0 && v->stats.agi > ctx.order[j]->stats.agi) {
            ctx.order[j + 1] = ctx.order[j];
            --j;
        }
        ctx.order[j + 1] = v;
    }
}

bool start_turn(BattleCtx& ctx, Actor& actor) {
    return status_on_turn_start(ctx, actor).can_act;
}

Actor* advance_turn(BattleCtx& ctx) {
    const int n = ctx.order.size();
    if (n == 0) return nullptr;

    for (int tries = 0; tries < n; ++tries) {
        ctx.turn_i = (ctx.turn_i + 1) % n;
        Actor* a = ctx.order[ctx.turn_i];
        if (alive(a)) {
            if (start_turn(ctx, *a)) return a;
            // Skipped/invalid turn still consumes duration
            status_on_turn_end(ctx, *a);
            status_tick_turns(ctx, *a);
        }
    }
    return nullptr;
}

} // namespace bt
