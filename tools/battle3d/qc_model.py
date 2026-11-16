"""qc_model.py - render QC of a converted model against a full-resolution reference conversion of
the same FBX (convert_model.py --tris 100000 --dissolve 0). Views: 6 yaws x 2 pitches, poses: bind
+ library clip frames. Metrics per view (as the earlier character QC, docs/character_qc):
  silhouette = pixels covered by exactly one of the two renders / pixels covered by the reference
  colour     = pixels covered by both whose colour differs by more than 48 (0..255, max channel)
Host tool:  python3 tools/battle3d/qc_model.py REF.dmf TEST.dmf [TEST2.dmf ...] --sheet out.jpg"""
import argparse, os, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmfio, preview, raster

BG = 0x102030

def render(m, lib, clip, t, yaw, pitch, size, weapons=(), holder=None):
    qs, ts = preview.pose_for(m, lib, clip, t)
    c = raster.Canvas(size, size, BG)
    el = np.radians(pitch); d = 6.2
    cam = raster.Camera((0, 1.05 + d * np.sin(el), d * np.cos(el)), (0, 1.05, 0), 40, size, size)
    world = preview.trs_y((0, 0, 0), yaw)
    skin = dmfio.skin_mats(m, qs, ts) if m['bones'] else None
    preview.draw_model(c, cam, m, world, skin, holder_tex=holder)
    img = np.clip(c.color + 0.5, 0, 255).astype(np.uint8)
    mask = c.depth > 0
    return img, mask

def compare(ref, test, lib, poses, size=256):
    rows = []
    for clip, t in poses:
        for pitch in (20, 45):
            for yaw in np.radians([0, 60, 120, 180, 240, 300]):
                a, ma = render(ref, lib, clip, t, yaw, pitch, size)
                b, mb = render(test, lib, clip, t, yaw, pitch, size)
                sil = (ma ^ mb).sum() / max(1, ma.sum())
                both = ma & mb
                col = ((np.abs(a.astype(int) - b.astype(int)).max(-1) > 48) & both).sum() / max(1, ma.sum())
                rows.append(dict(clip=clip, t=t, pitch=pitch, yaw=int(round(np.degrees(yaw))), sil=sil, col=col, a=a, b=b))
    return rows

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref'); ap.add_argument('tests', nargs='+')
    ap.add_argument('--dma', default='romdisk/anims/rig_medium.dma')
    ap.add_argument('--pose', action='append', default=[], help='clip@t (default: bind, idle_a@0.5, chop@0.6, run@0.3)')
    ap.add_argument('--sheet'); ap.add_argument('--size', type=int, default=256)
    a = ap.parse_args()
    ref = dmfio.read_dmf(a.ref); lib = dmfio.read_dma(a.dma)
    poses = [(p.split('@')[0], float(p.split('@')[1])) for p in a.pose] or [('bind', 0.0), ('idle_a', 0.5), ('chop', 0.6), ('run', 0.3)]
    if not ref['bones']: poses = [('bind', 0.0)]
    sheets = []
    for tp in a.tests:
        test = dmfio.read_dmf(tp)
        rows = compare(ref, test, lib, poses, a.size)
        ws = max(r['sil'] for r in rows); wc = max(r['col'] for r in rows)
        ms = np.mean([r['sil'] for r in rows]); mc = np.mean([r['col'] for r in rows])
        ntri = sum(b['num_tris'] for b in test['batches'])
        print('%-28s %5d tris  silhouette mean %.2f %% worst %.2f %%   colour mean %.2f %% worst %.2f %%' % (
            os.path.basename(tp), ntri, 100 * ms, 100 * ws, 100 * mc, 100 * wc))
        sheets.append((tp, rows))
    if a.sheet:
        pick = [0, 1, 3, 6, 13, 19, 25, 31]
        pick = [i for i in pick if i < len(sheets[0][1])]
        S = a.size // 2
        im = Image.new('RGB', (S * len(pick), S * (1 + len(sheets))), (0, 0, 0))
        d = ImageDraw.Draw(im)
        for j, i in enumerate(pick):
            r0 = sheets[0][1][i]
            im.paste(Image.fromarray(r0['a']).resize((S, S)), (S * j, 0))
            d.text((S * j + 2, 2), 'ref %s %d/%d' % (r0['clip'], r0['yaw'], r0['pitch']), fill=(255, 255, 255))
            for k, (tp, rows) in enumerate(sheets):
                r = rows[i]
                im.paste(Image.fromarray(r['b']).resize((S, S)), (S * j, S * (k + 1)))
                d.text((S * j + 2, S * (k + 1) + 2), '%s sil %.1f%%' % (os.path.basename(tp)[:14], 100 * r['sil']), fill=(255, 255, 0))
        im.save(a.sheet, quality=88)

if __name__ == '__main__':
    main()
