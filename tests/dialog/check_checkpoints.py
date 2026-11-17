#!/usr/bin/env python3
"""check_checkpoints.py - RAM checkpoints (GDD 10, 13 "Retry"; docs/NEXT_SLICE_DIALOG.md 5.6) on a --story trace:
after every retry (GAME OVER -> A) the live Progress equals, byte for byte, the Progress recorded when that
checkpoint was taken; the hero stands on its tile; HP is full; every map entity is gone exactly when its
`defeated` bit is set (an `if` entity may be hidden instead).

    python3 tests/dialog/check_checkpoints.py TRACE [--expect-retries N]
"""
import re
import sys

args = sys.argv[1:]
want = None
if '--expect-retries' in args:
    i = args.index('--expect-retries'); want = int(args[i + 1]); del args[i:i + 2]
trace = args[0]
saved = {}            # checkpoint bytes (hex) -> tick it was taken
last_cp = None
retries = 0
errs = []
pending_check = None
lines = open(trace).read().split('\n')
for idx, ln in enumerate(lines):
    if ln.startswith('Q '):
        raw = re.search(r' raw=([0-9a-f]+)', ln).group(1)
        cpraw = re.search(r' cpraw=([0-9a-f]+)', ln).group(1)
        r = int(re.search(r' retries=(\d+)', ln).group(1))
        tick = int(ln.split()[1])
        if cpraw != last_cp and r == retries:          # a checkpoint was taken this tick: the live bytes are its copy
            if raw != cpraw:
                errs.append('tick %d: the checkpoint copy differs from Progress at the moment it was taken' % tick)
            saved[cpraw] = tick
            last_cp = cpraw
        if r > retries:                                 # a retry happened this tick
            retries = r
            if raw != last_cp:
                errs.append('tick %d: after the retry Progress is %s, the checkpoint was %s' % (tick, raw, last_cp))
            pending_check = (tick, raw)
    elif ln.startswith('X mode=field') and pending_check:
        tick, raw = pending_check
        b = bytes.fromhex(raw)
        defeated = b[8] | b[9] << 8
        cp_x, cp_y = b[10], b[11]
        max_hp = b[2]
        m = re.search(r'hero=(-?\d+),(-?\d+),', ln)
        hx, hy = int(m.group(1)), int(m.group(2))
        tx, ty = (hx + 16 * 16) // 512, (hy + 24 * 16) // 512
        if (tx, ty) != (cp_x, cp_y):
            errs.append('tick %d: the hero is on %d,%d after the retry, the checkpoint tile is %d,%d' % (tick, tx, ty, cp_x, cp_y))
        hp = re.search(r' hp=(\d+)/(\d+)', ln)
        if int(hp.group(1)) != max_hp or int(hp.group(2)) != max_hp:
            errs.append('tick %d: HP %s/%s after the retry, want %d/%d' % (tick, hp.group(1), hp.group(2), max_hp, max_hp))
        ents = re.findall(r'(\w+):\d+,\d+:\d+,\d+:(alive|gone|hidden)', ln.split(' ents=')[1])
        for i, (eid, st) in enumerate(ents):
            gone = bool(defeated >> i & 1)
            if gone != (st == 'gone'):
                errs.append('tick %d: %s is %s after the retry, defeated bit %d' % (tick, eid, st, gone))
        pending_check = None
if want is not None and retries != want:
    errs.append('%d retries, expected %d' % (retries, want))
if errs:
    print('checkpoints: %d problems' % len(errs))
    for e in errs[:10]:
        print('  ' + e)
    sys.exit(1)
print('checkpoints: %d checkpoints taken, %d retries restored byte for byte (Progress, hero tile, full HP, entities)' % (len(saved), retries))
