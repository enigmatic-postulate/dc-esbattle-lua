#!/usr/bin/env python3
"""Prototype for the dialog slice plan (GDD 8): speaker portraits rendered from the game's own DMF
models with the project's reference renderer. Head-and-shoulders, 3/4 view (25 deg), posed with
library clips for the variants, per-character gradient, 128x128 quantised to RGB565, shown at 104x104
inside a 2 px gold frame. The production tool (tools/dialog/make_portraits.py) grows out of this.

    $BPY tools/battle3d/convert_model.py --fbx ../KayKit/Characters/Mage.fbx --texture ../KayKit/Characters/mage_texture.png \
        --tris 9000 --tex-size 512 --out /tmp/mage_hi.dmf     (same for Barbarian, Knight)
    python3 docs/dialog/proto/portraits.py out.png wren=/tmp/mage_hi.dmf brom=/tmp/barbarian_hi.dmf rowan=/tmp/knight_hi.dmf
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
import dmfio, raster, preview  # noqa: E402

LIB = dmfio.read_dma(os.path.join(ROOT, 'romdisk', 'anims', 'rig_medium.dma'))
YAW = {}  # per-character extra turn (degrees), e.g. to favour a hat brim
VARIANTS = [('neutral', 'idle_a', 0.5, 0.0), ('happy', 'interact', 0.55, 0.0), ('worried', 'hit_a', 0.3, 0.0)]
GRAD = {'wren': ((86, 52, 130), (22, 14, 44)), 'brom': ((140, 72, 34), (34, 18, 10)), 'rowan': ((44, 78, 132), (10, 18, 36))}


def to565(a):
    a = a.astype(np.uint32)
    r, g, b = (a[..., 0] * 31 + 127) // 255, (a[..., 1] * 63 + 127) // 255, (a[..., 2] * 31 + 127) // 255
    return np.stack([r * 255 // 31, g * 255 // 63, b * 255 // 31], -1).astype(np.uint8)


def portrait(m, name, clip, t, size=128):
    qs, ts = preview.pose_for(m, LIB, clip, t)
    W = dmfio.world_mats(m, qs, ts)
    names = [b['name'] for b in m['bones']]
    head = W[names.index('head')][:3, 3]
    target = head + np.array([0, 0.30, 0])
    yaw = np.radians(25)
    Y = preview.trs_y((0, 0, 0), np.radians(YAW.get(name, 0)))
    head = (Y @ np.r_[head, 1])[:3]
    target = head + np.array([0, 0.30, 0])
    eye = target + np.array([np.sin(yaw), 0.05, np.cos(yaw)]) * 3.4
    c = raster.Canvas(size, size, 0x000000)
    top, bot = GRAD.get(name, ((60, 60, 60), (20, 20, 20)))
    g = np.linspace(0, 1, size)[:, None]
    bg = (np.array(top) * (1 - g) + np.array(bot) * g)
    cam = raster.Camera(eye, target, 30, size, size)
    preview.draw_model(c, cam, m, preview.trs_y((0, 0, 0), np.radians(YAW.get(name, 0))), dmfio.skin_mats(m, qs, ts), light=np.array([0.5, 0.6, 0.62]), ambient=0.62, diffuse=0.5)
    img = np.array(c.image().convert('RGB')).astype(float)
    mask = (img.sum(-1) == 0)[..., None]
    img = np.where(mask, bg[:, None, :].repeat(size, 1), img)
    return Image.fromarray(to565(img.astype(np.uint8)))


def main():
    out = sys.argv[1]
    chars = [a.split('=', 1) for a in sys.argv[2:]]
    sheet = Image.new('RGB', (20 + 3 * 126, 20 + len(chars) * 126), (24, 20, 30))
    d = ImageDraw.Draw(sheet)
    for r, (name, path) in enumerate(chars):
        m = dmfio.read_dmf(path)
        for k, (var, clip, t, _) in enumerate(VARIANTS):
            p = portrait(m, name, clip, t).resize((104, 104), Image.BILINEAR)
            x, y = 10 + k * 126, 10 + r * 126
            d.rectangle([x, y, x + 107, y + 107], fill=(232, 184, 72))
            sheet.paste(p, (x + 2, y + 2))
            d.text((x + 4, y + 108), f'{name} {var}', fill=(230, 230, 230))
    sheet.save(out)
    print('wrote', out)


if __name__ == '__main__':
    main()
