#!/usr/bin/env python3
"""sheet.py - visual comparison sheets for the cel-shading study (docs/CEL_SHADING_STUDY.md).

    python3 tools/cel/sheet.py OUT.jpg --ticks 510,1000,... [--labels ...] [--configs base,toon,...]
        [--route tests/barrow/routes/full.route] [--crop x,y,w,h] [--cell 320x240] [--battle ENC:BOT:TICKS]

Rows = frames (ticks of a route played from the title), columns = prototype configurations (--tune sets, below).
Each frame is dumped by the host harness and rendered with tools/render_frame.py (the PVR-like rasteriser: the
hull's front-face culling and the fog are emulated). --crop zooms every cell on one region (x,y,w,h of 640x480).
"""
import argparse, os, subprocess, sys, tempfile
from PIL import Image, ImageDraw

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CONFIGS = {
    'base': [],
    'toon': ['cel_toon=1'],
    'outline 1.5': ['cel_lod=0', 'cel_zpush=0', 'cel_outline_px=1.5', 'cel_chars=1'],
    'outline 1.5 + toon': ['cel_lod=0', 'cel_zpush=0', 'cel_outline_px=1.5', 'cel_chars=1', 'cel_toon=1'],
    'outline 2.5 + toon': ['cel_lod=0', 'cel_zpush=0', 'cel_outline_px=2.5', 'cel_chars=1', 'cel_toon=1'],
    'all 1.5 + toon': ['cel_lod=0', 'cel_zpush=0', 'cel_outline_px=1.5', 'cel_chars=1', 'cel_props=2', 'cel_toon=1'],
    'small props 1.5': ['cel_lod=0', 'cel_zpush=0', 'cel_outline_px=1.5', 'cel_chars=0', 'cel_props=1'],
    'hero 1.5 + toon': ['cel_lod=0', 'cel_zpush=0', 'cel_outline_px=1.5', 'cel_chars=2', 'cel_toon=1'],
    'full hull 1.5': ['cel_outline_px=1.5', 'cel_chars=1', 'cel_lod=0', 'cel_zpush=0'],
    'lod hull 1.5': ['cel_outline_px=1.5', 'cel_chars=1', 'cel_lod=1'],
    'lod hull 2.0': ['cel_outline_px=2.0', 'cel_chars=1', 'cel_lod=1'],
    'lod hull 2.0 + toon': ['cel_outline_px=2.0', 'cel_chars=1', 'cel_lod=1', 'cel_toon=1'],
    'CEL=1: all chars lod 2.0 + toon': ['cel_outline_px=2.0', 'cel_chars=1', 'cel_lod=1', 'cel_toon=1', 'cel_zpush=0.05'],
    'CEL=2: hero lod 2.0 + toon': ['cel_outline_px=2.0', 'cel_chars=2', 'cel_lod=1', 'cel_toon=1', 'cel_zpush=0.05'],
}


def frames(cfg, ticks, route, battle, d, romdisk=None):
    cmd = [os.path.join(ROOT, 'build', 'battle_trace_f32'), '-', os.devnull, '--state-only', '--dump', d,
           '--dump-frames', ','.join(map(str, ticks))]
    if battle:
        enc, bot = battle
        cmd += ['--battle-test', enc, '--bot', bot, '--bot-wait', '120', '--bot-linger', '120']
    else:
        cmd += ['--route', route, '--boot', '--max-ticks', str(max(ticks) + 5)]
    tunes = cfg[len('custom:'):].split(';') if cfg.startswith('custom:') else CONFIGS[cfg]   # custom:name=v;name=v
    for t in tunes:
        cmd += ['--tune', t]
    if romdisk:
        cmd += ['--romdisk', romdisk]
    subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = []
    for t in ticks:
        f = os.path.join(d, 'frame_%05d.txt' % t)
        png = f.replace('.txt', '.png')
        if os.path.exists(f):
            subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'render_frame.py'), f, romdisk or os.path.join(ROOT, 'romdisk'), png])
            out.append(Image.open(png).convert('RGB'))
        else:
            out.append(Image.new('RGB', (640, 480), (60, 0, 0)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--ticks', required=True)
    ap.add_argument('--labels', default='')
    ap.add_argument('--configs', default='base,toon,outline 1.5 + toon')
    ap.add_argument('--route', default='tests/barrow/routes/full.route')
    ap.add_argument('--battle', default='')
    ap.add_argument('--crop', default='')
    ap.add_argument('--crops', default='', help='one x,y,w,h per tick, separated by ";"')
    ap.add_argument('--cell', default='320x240')
    ap.add_argument('--romdisk', default='', help='an alternative romdisk (e.g. other hull meshes); "cfg@DIR" per column also works')
    a = ap.parse_args()
    ticks = [int(t) for t in a.ticks.split(',')]
    labels = a.labels.split(',') if a.labels else [str(t) for t in ticks]
    cfgs = a.configs.split(',')
    cw, ch = (int(v) for v in a.cell.split('x'))
    battle = tuple(a.battle.split(':', 1)) if a.battle else None
    crops = [tuple(int(v) for v in c.split(',')) for c in a.crops.split(';')] if a.crops else \
        [tuple(int(v) for v in a.crop.split(','))] * len(ticks) if a.crop else [None] * len(ticks)
    cols = []
    for c in cfgs:
        d = tempfile.mkdtemp(prefix='cel_sheet_')
        name, _, rd = c.partition('@')
        cols.append(frames(name, ticks, a.route, battle, d, rd or a.romdisk or None))
    head = 18
    sheet = Image.new('RGB', (cw * len(cfgs), head + (ch + head) * len(ticks)), (16, 16, 20))
    dr = ImageDraw.Draw(sheet)
    for j, c in enumerate(cfgs):
        dr.text((j * cw + 6, 3), c, fill=(255, 230, 160))
    for i, t in enumerate(ticks):
        y = head + i * (ch + head)
        dr.text((6, y + 3), labels[i], fill=(200, 200, 200))
        for j in range(len(cfgs)):
            im = cols[j][i]
            if crops[i]:
                x0, y0, w, h = crops[i]
                im = im.crop((x0, y0, x0 + w, y0 + h))
            sheet.paste(im.resize((cw, ch), Image.LANCZOS), (j * cw, y + head))
    sheet.save(a.out, quality=90)
    print('wrote %s (%d x %d cells)' % (a.out, len(ticks), len(cfgs)))


if __name__ == '__main__':
    main()
