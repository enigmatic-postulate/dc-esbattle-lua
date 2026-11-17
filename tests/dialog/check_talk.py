#!/usr/bin/env python3
"""check_talk.py - the 'A Talk' prompt geometry (docs/NEXT_SLICE_DIALOG.md 5.1, GDD 7.2) against an independent
model, over battle_trace --probe-talk output (P <target> <hero x> <hero y> <face x> <face y>, 1/16 px).

Model: the hero's feet-box centre (sprite cell + (16, 24) px) to each NPC's tile centre; in range when
d^2 <= (1.3 x 512)^2 = 443023; in the 60 degree cone when dot(f, d) > 0 and 4 dot^2 >= |f|^2 |d|^2; the nearest
wins, ties go to the lower entity index. NPCs stand at home (the probe runs right after loading the map).

    python3 tests/dialog/check_talk.py PROBE romdisk/maps/village.txt
"""
import sys

probe, map_path = sys.argv[1], sys.argv[2]
ents = []
for ln in open(map_path):
    if ln.startswith('entity '):
        t = ln.split()
        ents.append((t[1], t[2], int(t[4]), int(t[5])))
npcs = [(i, e) for i, e in enumerate(ents) if e[1] == 'npc']
n = bad = hits = multi = ties = 0
per_npc = {}
first_bad = []
for ln in open(probe):
    if not ln.startswith('P '):
        continue
    _, tgt, hx, hy, fx, fy = ln.split()
    tgt, hx, hy, fx, fy = int(tgt), int(hx), int(hy), int(fx), int(fy)
    cx, cy = hx + 16 * 16, hy + 24 * 16
    best, best_d2, cands = -1, None, []
    for i, (eid, kind, tx, ty) in npcs:
        dx, dy = tx * 512 + 256 - cx, ty * 512 + 256 - cy
        d2 = dx * dx + dy * dy
        if d2 > 443023 or d2 == 0:
            continue
        dot = fx * dx + fy * dy
        if dot <= 0 or 4 * dot * dot < (fx * fx + fy * fy) * d2:
            continue
        cands.append(d2)
        if best < 0 or d2 < best_d2:
            best, best_d2 = i, d2
    if len(cands) > 1:
        multi += 1
        ties += sorted(cands)[0] == sorted(cands)[1]
    n += 1
    hits += best >= 0
    if best >= 0:
        per_npc[ents[best][0]] = per_npc.get(ents[best][0], 0) + 1
    if best != tgt:
        bad += 1
        if len(first_bad) < 5:
            first_bad.append('hero %d,%d facing %d,%d: game %d, model %d' % (hx, hy, fx, fy, tgt, best))
if bad or n == 0:
    print('talk prompt: %d of %d positions differ from the model' % (bad, n))
    for b in first_bad:
        print('  ' + b)
    sys.exit(1)
print('talk prompt: %d hero positions x facings match the model (1.3 tiles, 60 degrees, nearest, ties -> lower index); '
      'prompt on %d (%s), off on %d; two NPCs in reach %d times (%d exact ties)' % (n, hits, ', '.join('%s %d' % kv for kv in sorted(per_npc.items())), n - hits, multi, ties))
