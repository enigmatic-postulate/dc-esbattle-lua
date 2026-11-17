#!/usr/bin/env python3
"""sh4_model.py - an ESTIMATE of SH4 instructions per frame from the host's per-frame work counters (V lines),
calibrated on slice 3D-4's qemu measurement of the same field scripts. Not a measurement: the real one is
tests/dialog/sh4_budget.sh (SH4=1, needs the sh-elf toolchain and qemu of tests/sh4zam/README.md).

Model: game-code instructions = s x (40 xf + 24 out) + K + overlay, where xf = vertices transformed (skinned
characters and props: ~40 SH4 instructions each in the SH4ZAM build), out = vertices written to the arena
(~24 each: the strip copy), both measured per vertex on the SH4 build (docs/SLICE_SH4ZAM.md 4.2). s and K are
fitted so that the 3D-4 build's field frames (field_walk + field_fuzz2) reproduce their measured median
(0.95 M) and worst (1.33 M) instructions (docs/SLICE_3D_BATTLE.md). overlay (dialog): ~60 game-code
instructions per 2D sprite (text_draw, per glyph) and ~30 per rect; the KOS backend's sprite submission
(4 x pvr_prim per sprite, ~200 instructions) is reported separately, as the qemu bench's engine is a no-op.

    python3 tests/dialog/sh4_model.py NEW_V_TRACES...                       (the frozen calibration below)
    python3 tests/dialog/sh4_model.py --calibrate BASE_V_TRACES... -- NEW_V_TRACES...

The calibration data are the 3D-4 build's own counters: its tree (git HEAD of slice 3D-4) built with
`make host` and run with `--state-only --view` on tests/inputs/field_walk.in and field_fuzz2.in (8,276 field
frames) gives a work median of 536,376 and a worst of 786,408 (40 xf + 24 out). Frozen here so the suite does
not need the old tree; --calibrate recomputes them from such traces.
"""
import statistics, sys

MEASURED_MEDIAN, MEASURED_WORST = 0.95e6, 1.33e6
BASE_WORK_MEDIAN, BASE_WORK_WORST, BASE_FRAMES = 536376, 786408, 8276


def rows(paths, mode=None):
    out = []
    for p in paths:
        for ln in open(p):
            if ln.startswith('V '):
                d = dict(kv.split('=', 1) for kv in ln.split()[1:] if '=' in kv)
                if mode and d.get('mode') != mode:
                    continue
                out.append({k: int(v) for k, v in d.items() if v.lstrip('-').isdigit()} | {'mode': d.get('mode', 'field')})
    return out


def work(r):
    return 40 * r['xf'] + 24 * r['out']


args = sys.argv[1:]
if args and args[0] == '--calibrate':
    k = args.index('--')
    wb = [work(r) for r in rows(args[1:k])]
    wmed, wmax, nb = statistics.median(wb), max(wb), len(wb)
    new = args[k + 1:]
else:
    wmed, wmax, nb = BASE_WORK_MEDIAN, BASE_WORK_WORST, BASE_FRAMES
    new = args
s = (MEASURED_WORST - MEASURED_MEDIAN) / (wmax - wmed)
K = MEASURED_MEDIAN - s * wmed
print('calibration on %d 3D-4 field frames (qemu: median 0.95 M, worst 1.33 M): work median %d, worst %d -> s = %.3f, K = %.0f' % (nb, wmed, wmax, s, K))
for p in new:
    for mode in ('field', 'dialog'):
        rs = [r for r in rows([p]) if r['mode'] == mode]
        if not rs:
            continue
        est = [s * work(r) + K + 60 * r.get('sprites', 0) + 30 * r.get('rects', 0) for r in rs]
        kos = [200 * r.get('sprites', 0) for r in rs]
        print('%-28s %-6s %5d frames: median %.2f M, p99 %.2f M, worst %.2f M instructions (estimate); '
              'KOS 2D submission up to %.0f k more' % (p.split('/')[-1][:28], mode, len(rs), statistics.median(est) / 1e6,
                                                      sorted(est)[int(0.99 * len(est))] / 1e6, max(est) / 1e6, max(kos) / 1e3))
