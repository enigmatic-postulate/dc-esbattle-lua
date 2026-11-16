#!/usr/bin/env python3
"""Compares two builds on instructions (shcount.c) and operand-cache misses (shcache.c).

    python3 tests/sh4zam/summarize_cache.py DIR A B NAME [NAME...]

DIR holds NAME.A.txt / NAME.A.cache and NAME.B.txt / NAME.B.cache for each input script
(`sh4kit.sh count` and `sh4kit.sh cache` of the two bench ELFs on the same inputs). Only 3D field
frames are used (draw above 200k instructions in build A).

"Cycles" is a model, not a measurement: one cycle per instruction (no dual issue), plus the FPU's
long operations (FDIV/FSQRT 10, FTRV 3 more cycles), plus a fixed stall per demand miss (loads that
miss, and ordinary stores that miss - the copy-back cache reads the line first). The Dreamcast's
real miss cost is not modelled in detail (it depends on the RAM timing and on what a PREF had time
to hide), so the totals are given for a 20- and a 40-cycle miss.
"""
import statistics
import sys

CLASSES = ['all', 'fdiv', 'fsqrt', 'fsrra', 'fsca', 'ftrv', 'fipr', 'farith', 'fmem', 'branch', 'imem']
CACHE = ['rmiss', 'wmiss', 'wback', 'alloc', 'pref', 'phit']


def load(path, names):
    frames = []
    for line in open(path):
        if not line.startswith('frame'):
            continue
        t = line.split()
        n = len(names)
        tick = dict(zip(names, map(int, t[3:3 + n])))
        draw = dict(zip(names, map(int, t[4 + n:4 + 2 * n])))
        frames.append({k: tick[k] + draw[k] for k in names} | {'_draw_all': draw.get('all', 0)})
    return frames


def cycles(i, c, miss):
    return i['all'] + 10 * (i['fdiv'] + i['fsqrt']) + 3 * i['ftrv'] + miss * (c['rmiss'] + c['wmiss'])


def main():
    d, a, b, names = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    rows = {a: [], b: []}
    print(f"{'script':13s} {'frames':>6s}  {'instr ' + a:>12s} {'instr ' + b:>12s}  {'misses ' + a:>12s} {'misses ' + b:>12s}"
          f"  {'cyc@20':>7s} {'cyc@40':>7s}")
    for n in names:
        ia, ib = load(f'{d}/{n}.{a}.txt', CLASSES), load(f'{d}/{n}.{b}.txt', CLASSES)
        ca, cb = load(f'{d}/{n}.{a}.cache', CACHE), load(f'{d}/{n}.{b}.cache', CACHE)
        assert len(ia) == len(ib) == len(ca) == len(cb), n
        idx = [k for k in range(len(ia)) if ia[k]['_draw_all'] > 200000]
        if not idx:
            continue
        for tag, i, c in ((a, ia, ca), (b, ib, cb)):
            rows[tag] += [(i[k], c[k]) for k in idx]
        med = lambda f, L: statistics.median(f(x) for x in L)
        A = [(ia[k], ca[k]) for k in idx]
        B = [(ib[k], cb[k]) for k in idx]
        r20 = med(lambda x: cycles(x[0], x[1], 20), A) / med(lambda x: cycles(x[0], x[1], 20), B)
        r40 = med(lambda x: cycles(x[0], x[1], 40), A) / med(lambda x: cycles(x[0], x[1], 40), B)
        print(f"{n:13s} {len(idx):6d}  {med(lambda x: x[0]['all'], A):12,.0f} {med(lambda x: x[0]['all'], B):12,.0f}"
              f"  {med(lambda x: x[1]['rmiss'] + x[1]['wmiss'], A):12,.0f} {med(lambda x: x[1]['rmiss'] + x[1]['wmiss'], B):12,.0f}"
              f"  {r20:6.2f}x {r40:6.2f}x")
    print()
    print(f'All 3D field frames: {len(rows[a])}  (medians per frame, tick + draw)')
    for label, f in (('instructions', lambda x: x[0]['all']),
                     ('load misses', lambda x: x[1]['rmiss']),
                     ('store misses', lambda x: x[1]['wmiss']),
                     ('write-backs', lambda x: x[1]['wback']),
                     ('MOVCA.L allocations', lambda x: x[1]['alloc']),
                     ('PREF fills', lambda x: x[1]['pref']),
                     ('model cycles @20', lambda x: cycles(x[0], x[1], 20)),
                     ('model cycles @40', lambda x: cycles(x[0], x[1], 40))):
        va = statistics.median(f(x) for x in rows[a])
        vb = statistics.median(f(x) for x in rows[b])
        ratio = f'  ({va / vb:.2f}x)' if vb else ''
        print(f'  {label:20s} {va:>11,.0f} -> {vb:>11,.0f}{ratio}')
    for miss in (20, 40):
        va = statistics.median(cycles(x[0], x[1], miss) for x in rows[a])
        vb = statistics.median(cycles(x[0], x[1], miss) for x in rows[b])
        print(f'  median frame at 200 MHz, {miss}-cycle miss: {va / 200e3:.2f} ms -> {vb / 200e3:.2f} ms')


if __name__ == '__main__':
    main()
