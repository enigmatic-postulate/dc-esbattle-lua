#!/usr/bin/env python3
"""check_firelock.py - the learned-Fireball rule (GDD 11.1, slice 3D-5 G1) on the 3D battle's UI (U lines).
    python3 tests/dialog/check_firelock.py --write-input FILE
    python3 tests/dialog/check_firelock.py LOCKED_TRACE LEARNED_TRACE
Input: wait for the command ring, Right (to Fire), A, wait; Y (Guard shortcut); Left (Fire), A; Left (Attack), A.
Locked (--skills 0): A on Fire leaves the ring at home (nothing happens); Y still selects Guard; A on Attack enters
targeting. Learned (--skills 1): A on Fire enters the all-enemies confirm step."""
import sys

STEPS = [(0, 0)] * 200 + [(8, 1)] + [(0, 0)] * 20 + [(16, 1)] + [(0, 0)] * 30 + [(64, 1)] + [(0, 0)] * 20 + \
        [(4, 1)] + [(0, 0)] * 20 + [(16, 1)] + [(0, 0)] * 30 + [(4, 1)] + [(0, 0)] * 20 + [(16, 1)] + [(0, 0)] * 30
MARKS = {'fire_a': 200 + 1 + 20 + 1 + 25, 'y': 200 + 1 + 20 + 1 + 30 + 1 + 15, 'fire_a2': 200 + 1 + 20 + 1 + 30 + 1 + 20 + 1 + 20 + 1 + 25,
         'attack_a': len(STEPS) - 5}


def write(path):
    with open(path, 'w') as f:
        for b, _ in STEPS:
            f.write('0.0166667 %02x\n' % b)


def u_lines(path):
    out = []
    for ln in open(path):
        if ln.startswith('U '):
            d = dict(kv.split('=', 1) for kv in ln.split()[1:] if '=' in kv)
            out.append(d)
    return out


if sys.argv[1] == '--write-input':
    write(sys.argv[2]); sys.exit(0)
locked, learned = u_lines(sys.argv[1]), u_lines(sys.argv[2])
errs = []
def at(us, k):
    return us[k - 1] if k - 1 < len(us) else {}
L = at(locked, MARKS['fire_a'])
if not (L.get('state') == 'home' and L.get('home') == '1'):
    errs.append('locked: A on Fire changed the UI (%s)' % L)
if at(locked, MARKS['y']).get('home') != '2':
    errs.append('locked: Y did not select Guard (%s)' % at(locked, MARKS['y']))
if not (at(locked, MARKS['fire_a2']).get('state') == 'home'):
    errs.append('locked: a second A on Fire changed the UI')
if at(locked, MARKS['attack_a']).get('state') != 'target':
    errs.append('locked: A on Attack did not enter targeting (%s)' % at(locked, MARKS['attack_a']))
if at(learned, MARKS['fire_a']).get('state') != 'all':
    errs.append('learned: A on Fire did not enter the all-enemies step (%s)' % at(learned, MARKS['fire_a']))
if errs:
    print('fire lock: ' + '; '.join(errs)); sys.exit(1)
print('fire lock: locked - A on Fire ignored twice, Y still selects Guard, Attack works; learned - A on Fire confirms all enemies')
