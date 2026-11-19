#!/usr/bin/env python3
"""make_portraits.py - speaker portraits rendered from the game's own models (slice 3D-5, task E1; GDD 8).

    python3 tools/dialog/make_portraits.py [--hi DIR] [--out romdisk/portraits] [--sheet docs/dialog/portraits.png]

Source: high-detail conversions of the same KayKit FBX as the field models (decision D10), made with
tools/battle3d/convert_model.py --tris 9000 --tex-size 512 by tools/dialog/build_assets.sh into --hi
(build products, not shipped: no runtime cost). Posed with the shared animation library
(romdisk/anims/rig_medium.dma) for the variants:
    neutral = idle_a @ 0.5 s    happy = interact @ 0.55 s    worried = hit_a @ 0.3 s (head tilted away)
Framing: head and shoulders, a 3/4 view 25 degrees off the face, 30 degree lens (GDD 8). Light: a soft
three-point setup computed per vertex - a warm key from the camera's upper left, a cool fill from the
right, a rim from behind - on a per-character vertical gradient with a soft glow behind the head.
Rendered at 2x with the project's PVR-like rasteriser and box-filtered to 128x128 (anti-aliased edges),
then rounded to RGB565.

Output: romdisk/portraits/<name>_<variant>.dtx = 32-byte header ('DTX1', u16 w, h, fmt = 1 RGB565,
flags = 0, 20 zero bytes) + 128x128 linear texels (src/portrait.cpp; the KOS backend twiddles on upload).
Deterministic: the same inputs give the same bytes (tests/dialog/run.sh with BPY=).
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
import dmfio, raster, preview  # noqa: E402

SIZE = 128
SS = 2                                   # supersampling factor
VARIANTS = [('neutral', 'idle_a', 0.5), ('happy', 'interact', 0.55), ('worried', 'hit_a', 0.3)]
# name: (high-detail model file, gradient top, bottom, glow colour, extra yaw (deg), framing: head lift, distance)
CHARS = {
    'rowan': ('knight_hi.dmf',    (52, 88, 146), (10, 18, 38),  (120, 160, 220), 0.0, 0.50, 3.9),
    'wren':  ('mage_hi.dmf',      (96, 58, 142), (24, 14, 46),  (190, 140, 240), 0.0, 0.62, 4.3),
    'brom':  ('barbarian_hi.dmf', (150, 80, 38), (36, 18, 10),  (240, 170, 110), 0.0, 0.52, 4.0),
    # slice 3D-6: Corwin (the Ranger, his teal cloak) and the Bone Warden (the Skeleton Mage, the barrow's purple)
    'corwin': ('ranger_hi.dmf',   (40, 110, 120), (10, 28, 34), (140, 220, 210), 0.0, 0.52, 4.0),
    'warden': ('skeleton_mage_hi.dmf', (84, 48, 128), (14, 8, 28), (190, 130, 255), 0.0, 0.70, 4.4),
}
KEY = (np.array([-0.55, 0.55, 0.63]), np.array([1.00, 0.94, 0.84]), 0.62)   # direction (toward the light), colour, strength
FILL = (np.array([0.80, 0.15, 0.55]), np.array([0.70, 0.80, 1.00]), 0.22)
RIM = (np.array([0.25, 0.45, -0.86]), np.array([1.00, 0.95, 0.85]), 0.45)
AMBIENT = 0.50


def unit(v):
    return v / np.linalg.norm(v)


def vertex_colours(m, skin, world, eye):
    """Three-point lighting per vertex (ARGB), the rim from the view direction."""
    P, N = dmfio.skinned_positions(m, skin)
    Pw = (np.c_[P, np.ones(len(P))] @ world.T)[:, :3]
    Nw = N @ world[:3, :3].T
    Nw /= np.maximum(np.linalg.norm(Nw, axis=1, keepdims=True), 1e-9)
    V = unit(np.asarray(eye)[None, :] - Pw) if False else (np.asarray(eye)[None, :] - Pw)
    V /= np.maximum(np.linalg.norm(V, axis=1, keepdims=True), 1e-9)
    lit = np.full((len(P), 3), AMBIENT)
    for d, col, k in (KEY, FILL):
        lit += k * np.clip(Nw @ unit(d), 0, None)[:, None] * col[None, :]
    facing = np.clip((Nw * V).sum(1), 0, 1)
    rim = (1.0 - facing) ** 2.5 * np.clip(Nw @ unit(RIM[0]) + 0.35, 0, None)
    lit += RIM[2] * rim[:, None] * RIM[1][None, :]
    base = m['verts']['argb'].astype(np.uint64)
    rgb = np.stack([(base >> 16) & 255, (base >> 8) & 255, base & 255], 1).astype(float)
    out = np.minimum(rgb * lit, 255).astype(np.uint32)
    return (np.uint32(0xFF) << 24) | (out[:, 0] << 16) | (out[:, 1] << 8) | out[:, 2]


def background(top, bot, glow, size):
    y = np.linspace(0, 1, size)[:, None, None]
    bg = np.array(top)[None, None, :] * (1 - y) + np.array(bot)[None, None, :] * y
    yy, xx = np.mgrid[0:size, 0:size] / float(size)
    d = np.sqrt((xx - 0.46) ** 2 + ((yy - 0.38) * 1.15) ** 2)
    g = np.clip(1.0 - d / 0.55, 0, 1) ** 2 * 0.55
    bg = bg * (1 - g[..., None]) + np.array(glow)[None, None, :] * g[..., None] * 0.85 + bg * g[..., None] * 0.15
    return np.broadcast_to(bg, (size, size, 3)).copy()


def render(m, lib, name, clip, t):
    _, top, bot, glow, yaw_extra, lift, dist = CHARS[name]
    qs, ts = preview.pose_for(m, lib, clip, t)
    W = dmfio.world_mats(m, qs, ts)
    names = [b['name'] for b in m['bones']]
    world = preview.trs_y((0, 0, 0), np.radians(yaw_extra))
    head = (world @ np.r_[W[names.index('head')][:3, 3], 1])[:3]
    target = head + np.array([0, lift * 0.5, 0])
    yaw = np.radians(25)
    eye = target + np.array([np.sin(yaw), 0.06, np.cos(yaw)]) * dist
    S = SIZE * SS
    c = raster.Canvas(S, S, 0x000000)
    cam = raster.Camera(eye, target, 30, S, S)
    skin = dmfio.skin_mats(m, qs, ts)
    cols = vertex_colours(m, skin, world, eye)
    preview.draw_model(c, cam, m, world, skin, colors=cols)
    img = np.clip(c.color + 0.5, 0, 255)
    mask = (c.depth > 0).astype(float)
    bg = background(top, bot, glow, S)
    comp = img * mask[..., None] + bg * (1 - mask[..., None])
    # box filter SSxSS -> 128x128
    comp = comp.reshape(SIZE, SS, SIZE, SS, 3).mean((1, 3))
    return np.clip(np.floor(comp + 0.5), 0, 255).astype(np.uint32)


def to565(a):
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    return (((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)).astype('<u2')


def from565(t):
    t = t.astype(np.uint32)
    return np.stack([((t >> 11) & 31) * 255 // 31, ((t >> 5) & 63) * 255 // 63, (t & 31) * 255 // 31], -1).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hi', default=os.path.join(ROOT, 'build', 'portrait_src'), help='the high-detail .dmf conversions')
    ap.add_argument('--out', default=os.path.join(ROOT, 'romdisk', 'portraits'))
    ap.add_argument('--sheet', default=os.path.join(ROOT, 'docs', 'dialog', 'portraits.png'))
    a = ap.parse_args()
    lib = dmfio.read_dma(os.path.join(ROOT, 'romdisk', 'anims', 'rig_medium.dma'))
    os.makedirs(a.out, exist_ok=True)
    shots = {}
    for name, spec in CHARS.items():
        path = os.path.join(a.hi, spec[0])
        if not os.path.exists(path):
            sys.exit('missing %s: run tools/dialog/build_assets.sh (BPY=...) first' % path)
        m = dmfio.read_dmf(path)
        for var, clip, t in VARIANTS:
            px = render(m, lib, name, clip, t)
            tex = to565(px)
            with open(os.path.join(a.out, '%s_%s.dtx' % (name, var)), 'wb') as f:
                f.write(b'DTX1' + struct.pack('<4H20s', SIZE, SIZE, 1, 0, b'\0' * 20))
                f.write(tex.tobytes())
            shots[(name, var)] = from565(tex)
    # QC sheet: as the game shows them (104x104 in a 2 px gold frame, bilinear) and 1:1
    names = list(CHARS)
    W = 20 + 3 * 126 + 30 + 3 * 132
    sheet = Image.new('RGB', (W, 24 + len(names) * 140), (24, 20, 30))
    d = ImageDraw.Draw(sheet)
    d.text((10, 4), 'in game: 104x104 in a 2 px gold frame', fill=(230, 230, 230))
    d.text((20 + 3 * 126 + 30, 4), '128x128 RGB565 (1:1)', fill=(230, 230, 230))
    for r, name in enumerate(names):
        for k, (var, _, _) in enumerate(VARIANTS):
            im = Image.fromarray(shots[(name, var)])
            x, y = 10 + k * 126, 20 + r * 140
            d.rectangle([x, y, x + 107, y + 107], fill=(232, 184, 72))
            sheet.paste(im.resize((104, 104), Image.BILINEAR), (x + 2, y + 2))
            d.text((x + 4, y + 110), '%s %s' % (name, var), fill=(230, 230, 230))
            sheet.paste(im, (20 + 3 * 126 + 30 + k * 132, y))
    os.makedirs(os.path.dirname(a.sheet), exist_ok=True)
    sheet.save(a.sheet)
    print('wrote %d portraits to %s (128x128 RGB565, %d bytes each) and %s' % (len(shots), os.path.relpath(a.out, ROOT),
                                                                          32 + SIZE * SIZE * 2, os.path.relpath(a.sheet, ROOT)))


if __name__ == '__main__':
    main()
