#!/usr/bin/env python3
"""sweep_map.py - frame cost over every walkable tile (battle_trace --sweep S lines) as a heat map over the map,
plus the summary the budget check uses. The SH4 figure is the calibrated estimate of tests/dialog/sh4_model.py
(s x (40 transformed + 24 submitted) + K; not a measurement).

    python3 tools/vs1art/sweep_map.py SWEEP.txt MAP.txt [OUT.png] [--label TEXT]
"""
import argparse, statistics, sys
from PIL import Image, ImageDraw

S_CAL, K_CAL = 1.520, 134813          # tests/dialog/sh4_model.py's frozen calibration


def load(path):
    out = {}
    for ln in open(path):
        p = ln.split()
        if p and p[0] == 'S':
            out[(int(p[1]), int(p[2]))] = {k: int(v) for k, v in (kv.split('=') for kv in p[3:])}
    return out


def est(r):
    return S_CAL * (40 * r['xf'] + 24 * r['out']) + K_CAL


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('sweep'); ap.add_argument('map'); ap.add_argument('out', nargs='?')
    ap.add_argument('--label', default='')
    a = ap.parse_args()
    d = load(a.sweep)
    L = open(a.map).read().split('\n')
    i = next(k for k, l in enumerate(L) if l.startswith('map '))
    w, h = map(int, L[i].split()[1:3])
    ar = sorted(r['arena'] for r in d.values())
    e = sorted(est(r) for r in d.values())
    worst = max(d, key=lambda k: d[k]['arena'])
    print('%d tiles: written vertices median %d, p95 %d, max %d at %s; SH4 estimate median %.2f M, p95 %.2f M, max %.2f M; '
          'drops %d; near-plane props on %d tiles' % (
              len(d), statistics.median(ar), ar[int(0.95 * len(ar))], ar[-1], worst, statistics.median(e) / 1e6,
              e[int(0.95 * len(e))] / 1e6, e[-1] / 1e6, sum(r['drops'] for r in d.values()), sum(1 for r in d.values() if r.get('near'))))
    if not a.out:
        return 0
    C = 16
    img = Image.new('RGB', (w * C, h * C + 40), (30, 30, 34))
    dr = ImageDraw.Draw(img)
    ramp = [(0, (40, 90, 160)), (8000, (60, 170, 90)), (12000, (230, 210, 60)), (15000, (240, 140, 40)), (18000, (220, 40, 40))]
    def col(v):
        for (a0, c0), (a1, c1) in zip(ramp, ramp[1:]):
            if v <= a1:
                t = max(0.0, (v - a0) / (a1 - a0))
                return tuple(int(c0[j] + (c1[j] - c0[j]) * t) for j in range(3))
        return ramp[-1][1]
    for y in range(h):
        for x in range(w):
            r = d.get((x, y))
            if r:
                dr.rectangle([x * C, y * C, x * C + C - 1, y * C + C - 1], fill=col(r['arena']))
            else:
                dr.rectangle([x * C + 3, y * C + 3, x * C + C - 4, y * C + C - 4], fill=(90, 90, 90))
    dr.text((4, h * C + 4), '%s written vertices per frame, hero on each tile (grey: blocked). median %d, p95 %d, max %d' % (
        a.label, statistics.median(ar), ar[int(0.95 * len(ar))], ar[-1]), fill=(255, 255, 255))
    x0 = 4
    for v, c in ramp:
        dr.rectangle([x0, h * C + 22, x0 + 14, h * C + 34], fill=c)
        dr.text((x0 + 18, h * C + 22), str(v), fill=(255, 255, 255))
        x0 += 70
    img.save(a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
