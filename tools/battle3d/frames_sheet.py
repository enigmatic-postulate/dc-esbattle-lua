"""frames_sheet.py - renders host frame dumps (tools/render_frame.py) into a labelled contact sheet.
    python3 tools/battle3d/frames_sheet.py DUMP_DIR out.jpg 30,120,400 [--cols 3] [--trace T] [--scale 0.5]"""
import argparse, os, sys
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import render_frame
ap = argparse.ArgumentParser()
ap.add_argument('dump'); ap.add_argument('out'); ap.add_argument('frames')
ap.add_argument('--cols', type=int, default=3); ap.add_argument('--scale', type=float, default=0.5)
ap.add_argument('--trace'); ap.add_argument('--romdisk', default='romdisk')
a = ap.parse_args()
frames = [int(x) for x in a.frames.split(',')]
info = {}
if a.trace:
    n = 0
    for ln in open(a.trace):
        if ln.startswith('F ') and ln.split()[1] != 'boot': n = int(ln.split()[1])
        elif ln.startswith('B ') and n in frames: info[n] = ' '.join(ln.split()[1:3])
W, H = int(640 * a.scale), int(480 * a.scale)
rows = (len(frames) + a.cols - 1) // a.cols
sheet = Image.new('RGB', (W * a.cols, (H + 16) * rows), (16, 16, 20))
d = ImageDraw.Draw(sheet)
for i, f in enumerate(frames):
    im = render_frame.render(os.path.join(a.dump, 'frame_%05d.txt' % f), a.romdisk).resize((W, H), Image.BILINEAR)
    x, y = (i % a.cols) * W, (i // a.cols) * (H + 16)
    sheet.paste(im, (x, y + 16))
    d.text((x + 4, y + 2), 'tick %d  %s' % (f, info.get(f, '')), fill=(255, 255, 255))
sheet.save(a.out, quality=88)
