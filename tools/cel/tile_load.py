#!/usr/bin/env python3
"""tile_load.py - an ESTIMATE of the PowerVR's tile binning load of a host frame dump (docs/CEL_SHADING_STUDY.md).

    python3 tools/cel/tile_load.py FRAME.txt [FRAME2.txt ...]

The TA sorts every polygon into the 32x32-pixel tiles it touches and writes object pointers into each tile's object
list (OPB). A triangle strip is entered as segments of up to 6 triangles (one pointer per segment per tile it
touches, here: its bounding box). Each list starts in a block of the configured bin size (16 words for the OP and TR
lists in platform/kos/main.cpp; the last word links to the next block) and grows into blocks from the shared
overflow area. Reported per list: pointers in the frame, the busiest tile, and the 16-word blocks they need
(sum over tiles of ceil(pointers / 15)); the initial blocks are 300 (20 x 15 tiles), the rest come from overflow.
Approximate (bounding boxes, not exact triangle/tile tests): use it to compare frames, not as a limit.
"""
import math, sys

MAT_TRANSLUCENT, MAT_ADDITIVE, MAT_MULTIPLY = 1, 8, 16
TW, TH = 20, 15


def load(path):
    mats, lists = {}, {'OP': {}, 'TR': {}}
    lines = open(path).read().split('\n')
    i = 0
    while i < len(lines):
        p = lines[i].split()
        i += 1
        if not p:
            continue
        if p[0] == 'mat':
            mats[int(p[1])] = int(p[3])
        elif p[0] == 'b':
            m, n = int(p[1]), int(p[2])
            lst = 'TR' if mats.get(m, 0) & (MAT_TRANSLUCENT | MAT_ADDITIVE | MAT_MULTIPLY) else 'OP'
            cur = []
            for k in range(n):
                q = lines[i + k].split()
                cur.append((float(q[1]), float(q[2])))
                if q[0] == 'e':
                    add_strip(lists[lst], cur)
                    cur = []
            i += n
    return lists


def add_strip(tiles, pts):
    ntri = len(pts) - 2
    for s in range(0, max(ntri, 0), 6):
        seg = pts[s:s + 8]                       # up to 6 triangles = 8 vertices
        xs = [x for x, _ in seg]; ys = [y for _, y in seg]
        x0, x1 = max(0, int(min(xs) // 32)), min(TW - 1, int(max(xs) // 32))
        y0, y1 = max(0, int(min(ys) // 32)), min(TH - 1, int(max(ys) // 32))
        if x0 > x1 or y0 > y1:
            continue
        for ty in range(y0, y1 + 1):
            for tx in range(x0, x1 + 1):
                tiles[(tx, ty)] = tiles.get((tx, ty), 0) + 1


def summary(tiles):
    total = sum(tiles.values())
    peak = max(tiles.values()) if tiles else 0
    blocks = sum(math.ceil(v / 15) for v in tiles.values())
    return total, peak, blocks


def main():
    for f in sys.argv[1:]:
        L = load(f)
        parts = []
        for lst in ('OP', 'TR'):
            t, p, b = summary(L[lst])
            parts.append('%s: %d pointers, busiest tile %d, %d blocks (%d over the 300 initial)' % (lst, t, p, b, max(0, b - 300)))
        print('%s  %s' % (f.split('/')[-1], ' | '.join(parts)))


if __name__ == '__main__':
    main()
