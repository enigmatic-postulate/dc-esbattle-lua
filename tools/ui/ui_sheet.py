#!/usr/bin/env python3
"""ui_sheet.py - before / after sheet of the UI art pass (docs/UI_ART.md): the same frames drawn by an older build
(the 5x7 font and the 16x16 pixel icons) and by this one (tools/ui/make_ui_atlas.py's atlas).

    python3 tools/ui/ui_sheet.py OUT.jpg --old-exe /tmp/oldtree/build/battle_trace_f32 --old-romdisk /tmp/oldtree/romdisk

Scenes are host frame dumps rendered with tools/render_frame.py; each row: before | after, with an optional 2x crop.
"""
import argparse, os, subprocess, sys, tempfile
from PIL import Image, ImageDraw

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

# (label, harness args, tick, crop x,y,w,h or None)
ROUTE = ['-', os.devnull, '--state-only', '--boot', '--route', 'tests/barrow/routes/full.route']
SCENES = [
    ('battle: the command ring and the prompts', ['tests/battle3d/ui3d.in', os.devnull, '--battle-test', 'test_slimes', '--state-only'], 100, None),
    ('battle: choosing a target', ['tests/battle3d/ui3d.in', os.devnull, '--battle-test', 'test_slimes', '--state-only'], 140, None),
    ('battle: damage and burn numbers, the hero panel', ['-', os.devnull, '--battle-test', 'test_slimes', '--state-only', '--bot', 'cycle:AFG',
                                                         '--bot-wait', '120', '--bot-linger', '120'], 780, None),
    ('boss battle: the name and the bar', ['-', os.devnull, '--battle-test', 'warden', '--state-only', '--bot', 'boss:good:2',
                                           '--bot-wait', '120', '--bot-linger', '120'], 700, None),
    ('the boss won: VICTORY', ROUTE, 11150, None),
    ('field: the HUD and the talk prompt', ROUTE, 480, None),
    ('the title: PRESS START', ROUTE, 100, None),
    ('the boss intro: the name card', ROUTE, 5700, None),
    ('the death screen', ['-', os.devnull, '--state-only', '--boot', '--route', 'tests/barrow/routes/cp_c3.route'], 9800, None),
    ('the results card', ROUTE, 14600, None),
]


def frame(exe, romdisk, args, tick, d):
    cmd = [exe] + args + ['--max-ticks', str(tick + 5), '--dump', d, '--dump-frames', str(tick)]
    if romdisk:
        cmd += ['--romdisk', romdisk]
    subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    f = os.path.join(d, 'frame_%05d.txt' % tick)
    if not os.path.exists(f):
        return Image.new('RGB', (640, 480), (60, 0, 0))
    png = f.replace('.txt', '.png')
    subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'render_frame.py'), f, romdisk or os.path.join(ROOT, 'romdisk'), png])
    return Image.open(png).convert('RGB')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--old-exe', required=True)
    ap.add_argument('--old-romdisk', required=True)
    ap.add_argument('--only', default='', help='comma-separated scene indices')
    ap.add_argument('--cell', default='480x360')
    a = ap.parse_args()
    cw, ch = (int(v) for v in a.cell.split('x'))
    scenes = [SCENES[int(i)] for i in a.only.split(',')] if a.only else SCENES
    new_exe = os.path.join(ROOT, 'build', 'battle_trace_f32')
    head = 18
    sheet = Image.new('RGB', (2 * cw, head + (ch + head) * len(scenes)), (16, 16, 20))
    dr = ImageDraw.Draw(sheet)
    dr.text((6, 3), 'before (the 5x7 font, 16x16 pixel icons)', fill=(255, 230, 160))
    dr.text((cw + 6, 3), 'after (the UI atlas: Poppins Bold with baked outlines, rendered icons)', fill=(255, 230, 160))
    for i, (label, args, tick, crop) in enumerate(scenes):
        y = head + i * (ch + head)
        dr.text((6, y + 3), label, fill=(200, 200, 200))
        for j, (exe, rd) in enumerate([(a.old_exe, a.old_romdisk), (new_exe, None)]):
            im = frame(exe, rd, args, tick, tempfile.mkdtemp(prefix='ui_sheet_'))
            if crop:
                x0, y0, w, h = crop
                im = im.crop((x0, y0, x0 + w, y0 + h))
            sheet.paste(im.resize((cw, ch), Image.LANCZOS), (j * cw, y + head))
    sheet.save(a.out, quality=92)
    print('wrote %s (%d scenes)' % (a.out, len(scenes)))


if __name__ == '__main__':
    main()
