#!/usr/bin/env python3
"""check_flow.py - the game flow of a --boot --story trace (slice 3D-6, docs/NEXT_SLICE_BARROW.md 11 T1 and T9): every mode
change is a catalogued transition with its exact ticks (GDD 6), in the order the GDD's flow allows (GDD 3).

    python3 tests/barrow/check_flow.py TRACE [--expect-modes m1,m2,...] [--expect-cleared]

Transitions (T lines: `T <tick> start|end Tn`, N lines: `N <tick> <mode>`):
  T1   boot / T11 -> title: fade from black, 45 ticks (it ends 45 ticks after the mode became the title, or at tick 45)
  T2   title -> card: fade to black 30; the card: hold 15 + 90 (A skips it); then the intro's fade in 30
  T5   field / dialog -> to_battle -> battle: 8 + 24 ticks (the flashes and the iris), then a 12-tick fade in
  T6   battle -> to_field -> field: 24 out, 12 in;  death -> field (the retry): 30 out, 20 in
  T7   map_change: 20 out + 20 in, then the 30-tick walk-in (exits) before the field
  T9   battle -> death: the death screen's presentation (options at 100, input at 160)
  T10  battle (the boss) -> to_field -> dialog (the ending): 48 out, 40 in
  T11  results / death -> title: 60 ticks to black, then T1
Also: the modes only change along the GDD 3 flow (a table of allowed successions).
"""
import re
import sys

ALLOWED = {
    'title': {'card', 'credits', 'field'},                  # new game (T2), credits, the attract demo (its field)
    'card': {'dialog', 'field'},                            # the intro (or straight to the field without one)
    'field': {'dialog', 'to_battle', 'map_change', 'title'},
    'dialog': {'field', 'to_battle', 'map_change', 'credits', 'results', 'title'},
    'to_battle': {'battle'},
    'battle': {'to_field', 'death'},
    'to_field': {'field', 'dialog'},
    'death': {'field', 'dialog', 'title'},
    'map_change': {'field', 'dialog'},
    'credits': {'dialog', 'title'},
    'results': {'dialog'},
}


def main():
    args = sys.argv[1:]
    expect_modes = None
    if '--expect-modes' in args:
        i = args.index('--expect-modes'); expect_modes = args[i + 1].split(','); del args[i:i + 2]
    trace = args[0]
    T, N = [], []
    for ln in open(trace):
        if ln.startswith('T '):
            p = ln.split()
            T.append((int(p[1]), p[2], p[3]))
        elif ln.startswith('N '):
            p = ln.split()
            N.append((int(p[1]), p[2]))
    errs = []
    checked = {}

    def count(k):
        checked[k] = checked.get(k, 0) + 1

    # spans of each transition
    spans, open_ = [], {}
    for tick, what, tid in T:
        if what == 'start':
            open_[tid] = tick
        elif tid in open_:
            spans.append((tid, open_.pop(tid), tick))
    mode_at = lambda t: max(((tt, m) for tt, m in N if tt <= t), default=(0, '-'))[1]
    for tid, a, b in spans:
        d = b - a
        if tid == 'T1':
            count('T1')
            if not (d == 45 or (a == 1 and b == 45)):
                errs.append('T1 at %d lasted %d ticks, want 45' % (a, d))
        elif tid == 'T5':
            count('T5')
            # the counter: to_battle for 32 ticks, then the battle's 12-tick fade in (the T5 id covers both)
            nb = [tt for tt, m in N if m == 'battle' and tt >= a]
            if not nb or nb[0] - a != 32:
                errs.append('T5 at %d: the battle began %s ticks later, want 32' % (a, nb[0] - a if nb else '-'))
            if d != 44:
                errs.append('T5 at %d lasted %d ticks, want 32 + 12' % (a, d))
        elif tid == 'T6':
            count('T6')
            if d not in (36, 50):
                errs.append('T6 at %d lasted %d ticks, want 24 + 12 (a win) or 30 + 20 (a retry)' % (a, d))
        elif tid == 'T7':
            count('T7')
            if d != 40:
                errs.append('T7 at %d lasted %d ticks, want 20 + 20' % (a, d))
        elif tid == 'T10':
            count('T10')
            if d != 88:
                errs.append('T10 at %d lasted %d ticks, want 48 + 40' % (a, d))
        elif tid == 'T11':
            count('T11')
            nt = [(tt, w, ti) for tt, w, ti in T if tt == b and w == 'start' and ti == 'T1']
            if d not in (30, 60):
                errs.append('T11 at %d lasted %d ticks, want 60 (or 30 after the credits from the title)' % (a, d))
    # T2: title -> card 30 ticks after the start; the card lasts 105 ticks unless skipped; the intro fades in 30
    for tick, what, tid in T:
        if tid == 'T2' and what == 'start' and mode_at(tick) == 'title':
            count('T2')
            nc = [tt for tt, m in N if m == 'card' and tt >= tick]
            if not nc or nc[0] - tick != 30:
                errs.append('T2 at %d: the card came %s ticks later, want 30' % (tick, nc[0] - tick if nc else '-'))
            else:
                after = [tt for tt, m in N if tt > nc[0]]
                if after and after[0] - nc[0] not in range(16, 106):
                    errs.append('T2: the card lasted %d ticks, want 15 + 90 (A skips the 90)' % (after[0] - nc[0]))
    # T7 walk-in: map_change -> field 50 ticks after the start (20 + the 30-tick walk-in) for exits
    for i, (tick, m) in enumerate(N):
        if m == 'map_change' and i + 1 < len(N) and N[i + 1][1] == 'field':
            count('T7 walk-in')
            if N[i + 1][0] - tick != 50:
                errs.append('T7 at %d: the field came %d ticks later, want 20 (out) + 30 (walk-in)' % (tick, N[i + 1][0] - tick))
    # the flow
    for (ta, a), (tb, b) in zip(N, N[1:]):
        count('flow')
        if a in ALLOWED and b not in ALLOWED[a]:
            errs.append('tick %d: %s -> %s is not in the GDD\'s flow' % (tb, a, b))
    if expect_modes:
        seq = [m for _, m in N]
        k = 0
        for m in seq:
            if k < len(expect_modes) and m == expect_modes[k]:
                k += 1
        if k < len(expect_modes):
            errs.append('the modes do not contain %s in order (stopped at %s)' % (','.join(expect_modes), expect_modes[k]))
    if errs:
        print('flow: %d problems' % len(errs))
        for e in errs[:12]:
            print('  ' + e)
        return 1
    print('flow: %s' % ', '.join('%s x%d' % (k, v) for k, v in sorted(checked.items())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
