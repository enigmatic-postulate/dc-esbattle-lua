"""check_ui3d.py - UI rules on the 3D battle (slice 3D-4, DoD F3 / D2), battle-test test_slimes.
    python3 tests/battle3d/check_ui3d.py --write-input tests/battle3d/ui3d.in
    python3 tests/battle3d/check_ui3d.py TRACE      (a --view trace of ui3d.in)
Checks: A skips the intro; A enters targeting; A is ignored during the 16-tick entry lock; Right /
Left move the target cursor in SCREEN order (the enemies' projected x grows with the cursor index);
B goes back exactly one step (Attack still highlighted); Fire needs the confirming A and B backs out
of it with Fire highlighted; Y jumps to Guard; the 4-command ring (Attack, Fire, Guard, Potion)
wraps both ways; A on the Potion with none left does nothing; a confirmed Attack starts a sequence."""
import sys
A, B, Y, L, R = 0x10, 0x20, 0x40, 0x04, 0x08
PRESSES = [(6, A), (20, A), (22, A), (40, R), (44, R), (48, L), (52, B), (60, R), (64, A), (66, A), (90, B),
           (96, Y), (100, L), (104, L), (108, L), (112, A), (120, R), (130, A), (150, A)]
CHECKS = [  # tick, description, predicate on the U line dict (+ previous line)
    (20, 'A enters targeting (lock 16)', lambda u: u['state'] == 'target' and u['lock'] == 16),
    (22, 'A ignored during the entry lock', lambda u: u['state'] == 'target' and u['bstate'] == 'command'),
    (40, 'Right: next target', lambda u: u['target'] == 1),
    (40, 'cursor order == screen order (x grows left to right)', lambda u: all(a < b for a, b in zip(u['xs'], u['xs'][1:])) and len(u['xs']) == 3),
    (44, 'Right: third target', lambda u: u['target'] == 2),
    (48, 'Left: back to the second', lambda u: u['target'] == 1),
    (52, 'B: back to the ring, Attack highlighted', lambda u: u['state'] == 'home' and u['home'] == 0),
    (60, 'Right: Fire highlighted', lambda u: u['home'] == 1),
    (64, 'A on Fire: confirm step, every enemy marked', lambda u: u['state'] == 'all' and u['lock'] == 16),
    (66, 'A ignored during the lock (Fire not cast)', lambda u: u['state'] == 'all' and u['bstate'] == 'command'),
    (90, 'B: back to the ring, Fire highlighted', lambda u: u['state'] == 'home' and u['home'] == 1),
    (96, 'Y: Guard highlighted', lambda u: u['home'] == 2),
    (100, 'Left wraps down', lambda u: u['home'] == 1),
    (104, 'Left', lambda u: u['home'] == 0),
    (108, 'Left wraps to the Potion (4th command)', lambda u: u['home'] == 3),
    (112, 'A on the Potion with none left: nothing happens', lambda u: u['home'] == 3 and u['state'] == 'home' and u['busy'] == 0 and u['bstate'] == 'command'),
    (120, 'Right wraps to Attack', lambda u: u['home'] == 0),
    (130, 'A: targeting again', lambda u: u['state'] == 'target'),
    (152, 'A after the lock: the attack starts (sequence busy)', lambda u: u['busy'] == 1 and u['bstate'] in ('animate', 'resolve')),
]
if len(sys.argv) > 2 and sys.argv[1] == '--write-input':
    n = max(t for t, _ in PRESSES) + 40
    btn = {t: b for t, b in PRESSES}
    with open(sys.argv[2], 'w') as f:
        f.write('# tests/battle3d/check_ui3d.py --write-input (one line per tick; one-tick presses)\n')
        for t in range(1, n + 1):
            f.write('0.0166667 %02x\n' % btn.get(t, 0))
    sys.exit(0)
lines, tick = {}, 0
for ln in open(sys.argv[1]):
    if ln.startswith('F ') and ln.split()[1] != 'boot': tick = int(ln.split()[1])
    elif ln.startswith('U '):
        d = {}
        for kv in ln.split()[1:]:
            k, v = kv.split('=', 1)
            d[k] = [float(x) for x in v.split(',') if x] if k == 'xs' else (int(v) if v.lstrip('-').isdigit() else v)
        lines[tick] = d
bad = [f'tick {t}: {name} ({lines.get(t)})' for t, name, ok in CHECKS if t not in lines or not ok(lines[t])]
intro_skipped = lines.get(8, {}).get('busy', 1) == 0 or lines.get(10, {}).get('busy', 1) == 0
if not intro_skipped: bad.append('A did not skip the intro')
print('UI 3D: %d checks %s' % (len(CHECKS) + 1, 'OK' if not bad else 'FAILED\n  ' + '\n  '.join(bad)))
sys.exit(1 if bad else 0)
