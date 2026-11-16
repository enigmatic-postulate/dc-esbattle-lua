#!/usr/bin/env python3
"""Summarises shcount.c output for a baseline and an SH4ZAM build of the same input scripts.

    python3 tests/sh4zam/summarize.py DIR NAME [NAME...]

DIR holds NAME.base.txt and NAME.new.txt for each input script. Only 3D field frames are
summarised (baseline draw above 200k instructions; the 2D battle frames are skipped). The cycle
figure is a rough model, not a measurement: one cycle per instruction, plus the extra pipeline
cycles of the SH4's long floating-point operations (FDIV and FSQRT block the FPU for about 10
more cycles, FTRV for 3); it ignores dual issue, stalls on results and cache misses.
"""
import statistics
import sys

CLASSES = ['all', 'fdiv', 'fsqrt', 'fsrra', 'fsca', 'ftrv', 'fipr', 'farith', 'fmem', 'branch', 'imem']
EXTRA = {'fdiv': 10, 'fsqrt': 10, 'ftrv': 3}


def load(path):
    frames = []
    for line in open(path):
        if not line.startswith('frame'):
            continue
        t = line.split()
        tick = dict(zip(CLASSES, map(int, t[3:3 + len(CLASSES)])))
        draw = dict(zip(CLASSES, map(int, t[4 + len(CLASSES):4 + 2 * len(CLASSES)])))
        frames.append((tick, draw))
    return frames


def cycles(c):
    return c['all'] + sum(c[k] * v for k, v in EXTRA.items())


def total(f, key='all'):
    tick, draw = f
    return tick[key] + draw[key] if key != 'cycles' else cycles(tick) + cycles(draw)


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))]


def main():
    d, names = sys.argv[1], sys.argv[2:]
    allb, alln = [], []
    print(f"{'script':14s} {'frames':>6s}  {'baseline median':>16s} {'SH4ZAM median':>14s} {'ratio':>6s}   {'baseline max':>13s} {'SH4ZAM max':>11s}")
    for n in names:
        b, s = load(f'{d}/{n}.base.txt'), load(f'{d}/{n}.new.txt')
        assert len(b) == len(s), n
        idx = [i for i in range(len(b)) if b[i][1]['all'] > 200000]
        if not idx:
            continue
        allb += [b[i] for i in idx]
        alln += [s[i] for i in idx]
        mb = statistics.median(total(b[i]) for i in idx)
        ms = statistics.median(total(s[i]) for i in idx)
        print(f"{n:14s} {len(idx):6d}  {mb:16,.0f} {ms:14,.0f} {mb / ms:6.2f}x  {max(total(b[i]) for i in idx):13,d} {max(total(s[i]) for i in idx):11,d}")
    print()
    print(f'All 3D field frames: {len(allb)}')
    for key in ('all', 'cycles'):
        tb = [total(f, key) for f in allb]
        tn = [total(f, key) for f in alln]
        label = 'instructions' if key == 'all' else 'model cycles'
        print(f'  {label:13s} per frame (tick + draw): median {statistics.median(tb):>11,.0f} -> {statistics.median(tn):>11,.0f}'
              f'  ({statistics.median(tb) / statistics.median(tn):.2f}x);  p90 {pct(tb, 0.9):>11,d} -> {pct(tn, 0.9):>11,d};'
              f'  worst {max(tb):>11,d} -> {max(tn):>11,d} ({max(tb) / max(tn):.2f}x)')
    print('  per-frame means by class (tick + draw):')
    for k in CLASSES[1:]:
        mb = statistics.mean(total(f, k) for f in allb)
        mn = statistics.mean(total(f, k) for f in alln)
        print(f'    {k:7s} {mb:11,.0f} -> {mn:11,.0f}')
    # time at 200 MHz, instruction-per-cycle bounds
    mb = statistics.median(total(f, 'cycles') for f in allb)
    mn = statistics.median(total(f, 'cycles') for f in alln)
    print(f'  median frame at 200 MHz (model cycles only, no cache misses): {mb / 200e3:.2f} ms -> {mn / 200e3:.2f} ms')


if __name__ == '__main__':
    main()
