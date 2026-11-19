#!/usr/bin/env python3
"""sprite_cost.py - the 2D cost of the UI art pass (docs/UI_ART.md): sprites and rects per frame, old build vs this one.

    python3 tools/ui/sprite_cost.py [--old-exe EXE --old-romdisk DIR]

Runs the same scenarios with draw traces (S = one sprite, R = one rect; F = a frame boundary) and reports, per
scenario, the worst and mean sprites per frame and the SH4 instructions they cost (the calibrated model of
tests/dialog/sh4_model.py: ~60 game-code instructions per sprite + ~200 in the KOS submission, 4 x pvr_prim; ~30 per
rect). Without --old-exe only this build is measured.
"""
import argparse, os, subprocess, sys, tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SCEN = [
    ('battle UI (command ring, target)', ['tests/battle3d/ui3d.in', '@T', '--battle-test', 'test_slimes']),
    ('battle: squad (damage numbers)', ['-', '@T', '--battle-test', 'test_slimes', '--bot', 'cycle:AFG', '--bot-wait', '120', '--bot-linger', '120']),
    ('battle: the Bone Warden', ['-', '@T', '--battle-test', 'warden', '--bot', 'boss:good:2', '--bot-wait', '120', '--bot-linger', '120']),
    ('playthrough (title -> results -> title)', ['-', '@T', '--boot', '--route', 'tests/barrow/routes/full.route', '--max-ticks', '90000']),
    ('death screen route (cp_c3)', ['-', '@T', '--boot', '--route', 'tests/barrow/routes/cp_c3.route', '--max-ticks', '90000']),
]


def measure(exe, romdisk, args):
    d = tempfile.mkdtemp(prefix='ui_cost_')
    tr = os.path.join(d, 't.trace')
    cmd = [exe] + [tr if a == '@T' else a for a in args]
    if romdisk:
        cmd += ['--romdisk', romdisk]
    subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3000)
    frames, s, r = [], 0, 0
    with open(tr) as f:
        for ln in f:
            c = ln[:2]
            if c == 'S ':
                s += 1
            elif c == 'R ':
                r += 1
            elif c == 'F ':
                frames.append((s, r)); s = r = 0
    os.remove(tr)
    frames.append((s, r))
    frames = [x for x in frames if x != (0, 0)] or [(0, 0)]
    ws = max(x[0] for x in frames)
    wr = max(x[1] for x in frames)
    worst = max(260 * x[0] + 30 * x[1] for x in frames)
    return dict(frames=len(frames), worst_s=ws, mean_s=sum(x[0] for x in frames) / len(frames), worst_r=wr, worst_instr=worst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old-exe')
    ap.add_argument('--old-romdisk')
    a = ap.parse_args()
    new = os.path.join(ROOT, 'build', 'battle_trace_f32')
    print('%-42s %28s %28s' % ('scenario', 'old: sprites worst/mean (k instr)', 'new: sprites worst/mean (k instr)'))
    for name, args in SCEN:
        n = measure(new, None, args)
        o = measure(a.old_exe, a.old_romdisk, args) if a.old_exe else None
        fmt = lambda m: '%5d / %6.1f (%5.1f k)' % (m['worst_s'], m['mean_s'], m['worst_instr'] / 1000.0)
        print('%-42s %28s %28s' % (name, fmt(o) if o else '-', fmt(n)))


if __name__ == '__main__':
    main()
