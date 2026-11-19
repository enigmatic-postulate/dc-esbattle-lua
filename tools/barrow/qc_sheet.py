#!/usr/bin/env python3
"""qc_sheet.py - a contact sheet of host-rendered frames of a route (slice 3D-6 QC; docs/barrow/*.jpg).

    python3 tools/barrow/qc_sheet.py OUT.jpg --route tests/barrow/routes/full.route --ticks 100,200,... [--boot]
        [--labels a,b,...] [--cols 4] [--size 320x240]

Runs build/battle_trace_f32 with --dump for the ticks, renders each frame with tools/render_frame.py (the PVR-like
rasteriser, fog and MAT_MULTIPLY emulated) and lays them out with a caption each. Needs `make host`.
"""
import argparse, os, subprocess, sys, tempfile
from PIL import Image, ImageDraw

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--route', required=True)
    ap.add_argument('--ticks', required=True)
    ap.add_argument('--labels', default='')
    ap.add_argument('--boot', action='store_true')
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--size', default='320x240')
    a = ap.parse_args()
    ticks = [int(t) for t in a.ticks.split(',')]
    labels = a.labels.split(',') if a.labels else [str(t) for t in ticks]
    w, h = (int(v) for v in a.size.split('x'))
    d = tempfile.mkdtemp(prefix='qc_sheet_')
    cmd = [os.path.join(ROOT, 'build', 'battle_trace_f32'), '-', os.devnull, '--state-only', '--route', a.route, '--max-ticks',
           str(max(ticks) + 10), '--dump', d, '--dump-frames', ','.join(map(str, ticks))]
    if a.boot:
        cmd.append('--boot')
    subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ims = []
    for t, lab in zip(ticks, labels):
        f = os.path.join(d, 'frame_%05d.txt' % t)
        png = f.replace('.txt', '.png')
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'render_frame.py'), f, os.path.join(ROOT, 'romdisk'), png])
        im = Image.open(png).convert('RGB').resize((w, h), Image.LANCZOS)
        dr = ImageDraw.Draw(im)
        dr.rectangle([0, h - 16, w, h], fill=(0, 0, 0))
        dr.text((4, h - 14), lab, fill=(255, 230, 150))
        ims.append(im)
    cols = a.cols
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w, rows * h), (16, 16, 20))
    for i, im in enumerate(ims):
        sheet.paste(im, ((i % cols) * w, (i // cols) * h))
    sheet.save(a.out, quality=88)
    print('wrote %s (%d frames)' % (a.out, len(ims)))


if __name__ == '__main__':
    main()
