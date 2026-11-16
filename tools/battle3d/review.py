"""review.py - one row per sequence of a battle: frames at fixed fractions of each sequence's
duration (from the trace's B lines), rendered from a --dump-every 1 dump. Host tool for reviews.
    python3 tools/battle3d/review.py TRACE DUMP_DIR out.jpg [--kinds melee,bolt] [--max-rows 8] [--cols 6]"""
import argparse, os, sys
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import render_frame
ap = argparse.ArgumentParser()
ap.add_argument('trace'); ap.add_argument('dump'); ap.add_argument('out')
ap.add_argument('--kinds', default=''); ap.add_argument('--max-rows', type=int, default=8); ap.add_argument('--cols', type=int, default=6)
ap.add_argument('--scale', type=float, default=0.4); ap.add_argument('--romdisk', default='romdisk')
a = ap.parse_args()
seqs, cur, f = [], None, 0
for ln in open(a.trace):
    if ln.startswith('F ') and ln.split()[1] != 'boot': f = int(ln.split()[1])
    elif ln.startswith('B '):
        k = ln.split()[1].split('=')[1]
        if cur and (k != cur[0] or ln.split()[2] == 't=1'):
            seqs.append((cur[0], cur[1], f - 1)); cur = None
        if k != 'none' and cur is None: cur = (k, f)
if cur: seqs.append((cur[0], cur[1], f))
kinds = set(a.kinds.split(',')) if a.kinds else None
rows = [s for s in seqs if (kinds is None or s[0] in kinds)][:a.max_rows]
W, H = int(640 * a.scale), int(480 * a.scale)
sheet = Image.new('RGB', (W * a.cols, (H + 14) * len(rows)), (16, 16, 20))
d = ImageDraw.Draw(sheet)
for r, (k, s, e) in enumerate(rows):
    for c in range(a.cols):
        fr = s + (e - s) * c // (a.cols - 1)
        p = os.path.join(a.dump, 'frame_%05d.txt' % fr)
        if not os.path.exists(p): continue
        im = render_frame.render(p, a.romdisk).resize((W, H), Image.BILINEAR)
        sheet.paste(im, (c * W, r * (H + 14) + 14))
        d.text((c * W + 3, r * (H + 14) + 1), '%s %d/%d (tick %d)' % (k, fr - s, e - s, fr), fill=(255, 255, 255))
sheet.save(a.out, quality=85)
print('\n'.join('%s %d-%d' % x for x in seqs[:40]))
