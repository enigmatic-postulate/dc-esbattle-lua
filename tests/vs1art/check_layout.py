#!/usr/bin/env python3
"""check_layout.py - the shipped village map against VS1's (assets/vs1_art/village_map_vs1.txt) and the GDD.

  * tiles identical;
  * every VS1 prop is placed at the same footprint: VS1 gives the top-left tile of the footprint after rotation
    (catalog assets/vs1_art/models/props.txt), ours the centre and the solid size (field.cpp) - converted here;
    props that VS1 makes non-blocking are non-blocking; the rotation is the same;
  * the only deliberate difference is the wagon (moved out of the patrols' path for the frame budget,
    docs/SLICE_VS1_ART.md), checked to be where the doc says;
  * GDD 6/9 positions: Rowan spawns at the west gate (2,15), Wren at (6,17), Brom at (21,10), the patrols at
    (31,6) and (26,15).

    python3 tests/vs1art/check_layout.py [OURS VS1 CATALOG]
"""
import os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
MOVED = {'wagon': (10.0, 6.0)}          # deliberate moves (model -> our centre)
GDD = {'spawn': (2, 15), 'wren': (6, 17), 'brom': (21, 10), 'slime_a': (31, 6), 'slime_b': (26, 15)}


def read(path):
    L = open(path).read().split('\n')
    i = next(k for k, l in enumerate(L) if l.startswith('map '))
    w, h = map(int, L[i].split()[1:3])
    rows = L[i + 1:i + 1 + h]
    rest = [l.split() for l in L[i + 1 + h:] if l.strip() and not l.startswith('#')]
    return w, h, rows, rest


def main():
    a = sys.argv[1:]
    ours = a[0] if a else os.path.join(ROOT, 'romdisk/maps/village.txt')
    vs1 = a[1] if len(a) > 1 else os.path.join(ROOT, 'assets/vs1_art/village_map_vs1.txt')
    cat_path = a[2] if len(a) > 2 else os.path.join(ROOT, 'assets/vs1_art/models/props.txt')
    cat = {}
    for l in open(cat_path):
        if l.strip() and not l.startswith('#'):
            n, fw, fh, blocks, fades = l.split()
            cat[n] = (int(fw), int(fh), blocks == '1')
    w, h, rows, rest = read(ours)
    w1, h1, rows1, rest1 = read(vs1)
    errs = []
    if (w, h, rows) != (w1, h1, rows1):
        errs.append('tiles differ from VS1')
    want = []
    for p in rest1:
        if p[0] != 'prop':
            continue
        model, x, y, yaw = p[2], int(p[3]), int(p[4]), int(p[5]) if len(p) > 5 else 0
        fw, fh, blocks = cat[model]
        if yaw in (90, 270):
            fw, fh = fh, fw
        cx, cy = x + fw / 2, y + fh / 2
        if model in MOVED:
            cx, cy = MOVED[model]
        want.append((model, cx, cy, yaw, (fw, fh) if blocks else None))
    got = []
    for p in rest:
        if p[0] != 'prop':
            continue
        solid = (int(p[6]), int(p[7])) if len(p) >= 8 and p[5] == 'solid' else None
        got.append((p[1], float(p[2]), float(p[3]), int(float(p[4])), solid))
    if sorted(want) != sorted(got):
        errs.append('props differ: missing %s, extra %s' % (sorted(set(want) - set(got)), sorted(set(got) - set(want))))
    ents = {p[1]: (int(p[4]), int(p[5])) for p in rest if p[0] == 'entity'}
    spawn = next((int(p[1]), int(p[2])) for p in rest if p[0] == 'spawn')
    have = dict(ents, spawn=spawn)
    for k, v in GDD.items():
        if have.get(k) != v:
            errs.append('%s at %s, the GDD says %s' % (k, have.get(k), v))
    if errs:
        print('\n'.join(errs)); return 1
    print('village == VS1: %dx%d tiles, %d props at VS1\'s footprints (the wagon moved, as documented); GDD positions: %s' % (
        w, h, len(got), ', '.join('%s %s' % (k, v) for k, v in GDD.items())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
