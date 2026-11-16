"""clip_sheet.py - contact sheet of library clips on a model (frames across, one row per clip),
with the ground plane drawn and event frames marked. Host tool (no Blender).
    python3 tools/battle3d/clip_sheet.py romdisk/models/skel_warrior.dmf out.jpg [clip ...] [--weapon slot=path]"""
import argparse, os, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmfio, preview, raster

def ground(canvas, cam, half=1.5, y=0.0):
    rv = []
    for i in range(-3, 3):
        for j in range(-3, 3):
            x0, z0 = i * half / 3, j * half / 3
            x1, z1 = x0 + half / 3, z0 + half / 3
            col = 0xFF5A5040 if (i + j) % 2 else 0xFF6A6050
            P = np.array([[x0, y, z0], [x0, y, z1], [x1, y, z1], [x1, y, z0]])
            S, W = cam.project(P)
            if (W < cam.near_w).any(): continue
            pts = [(S[k, 0], S[k, 1], S[k, 2], 0, 0, col) for k in range(4)]
            rv += [pts[0], pts[1], pts[2], pts[0], pts[2], pts[3]]
    canvas.tris(np.array(rv, raster.RV), None, False, raster.OP, False)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dmf'); ap.add_argument('out'); ap.add_argument('clips', nargs='*')
    ap.add_argument('--dma', default='romdisk/anims/rig_medium.dma')
    ap.add_argument('--frames', type=int, default=8); ap.add_argument('--cell', type=int, default=200)
    ap.add_argument('--weapon', action='append', default=[]); ap.add_argument('--yaw', type=float, default=1.1)
    a = ap.parse_args()
    m = dmfio.read_dmf(a.dmf); lib = dmfio.read_dma(a.dma)
    clips = [c for c in lib['clips'] if not a.clips or c['name'] in a.clips]
    weapons = [(w.split('=')[0], dmfio.read_dmf(w.split('=')[1]), np.eye(4)) for w in a.weapon]
    tex = preview.load_tex(m)
    C = a.cell
    sheet = Image.new('RGB', (C * a.frames + 90, C * len(clips)), (20, 22, 28))
    remap = [lib['bones'].index(b['name']) for b in m['bones']]
    for r, c in enumerate(clips):
        dur = (c['frames'] - (0 if c['loop'] else 1)) / c['fps']
        evs = [e for e in c['events'] if e != 0xFFFF]
        times = [dur * i / (a.frames - 1) for i in range(a.frames)]
        for k, e in enumerate(evs[:2]):          # replace the nearest columns by the event frames
            j = int(np.argmin([abs(t - e / 60.0) for t in times])); times[j] = e / 60.0
        for i, t in enumerate(times):
            qs, ts = dmfio.sample_clip(m, c, t, remap)
            cv = raster.Canvas(C, C, 0x2A2F3A)
            cam = raster.Camera((0, 2.4, 6.0), (0, 0.9, 0), 40, C, C)
            ground(cv, cam)
            world = preview.trs_y((0, 0, 0), a.yaw)
            preview.draw_model(cv, cam, m, world, dmfio.skin_mats(m, qs, ts))
            for slot, wm, off in weapons:
                preview.draw_model(cv, cam, wm, world @ preview.bone_world(m, qs, ts, slot) @ off, None, holder_tex=tex)
            im = cv.image()
            d = ImageDraw.Draw(im)
            is_ev = any(abs(t - e / 60.0) < 1e-6 for e in evs)
            d.text((3, 2), '%.2fs%s' % (t, ' EVENT' if is_ev else ''), fill=(255, 220, 90) if is_ev else (200, 200, 200))
            sheet.paste(im, (90 + C * i, C * r))
        d = ImageDraw.Draw(sheet)
        d.text((4, C * r + 4), c['name'], fill=(255, 255, 255))
        d.text((4, C * r + 18), '%d fr %s' % (c['frames'], 'loop' if c['loop'] else ''), fill=(160, 160, 160))
        d.text((4, C * r + 32), 'ev ' + ','.join(str(e) for e in evs), fill=(255, 220, 90))
    sheet.save(a.out, quality=85)

if __name__ == '__main__':
    main()
