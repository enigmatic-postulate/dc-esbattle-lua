#!/usr/bin/env python3
"""make_logo.py - the title logo (slice 3D-6, GDD 5; the stand-in for asset A3, decision D9): "PROJECT HYMN" and
"The Restless Dead" typeset with the dialog font's TTF (assets/fonts/HomeVideo-Regular.ttf, CC0) into a 256x64
ARGB4444 texture, drawn at 2x by the title (src/screens.cpp). A pixel font: rendered without anti-aliasing at whole
multiples of its design grid (the title at 3 px per design pixel, the subtitle at 2), a warm vertical gradient on the
title, a 2 px dark outline and a soft drop shadow in the alpha channel.

    python3 tools/make_logo.py [--check] [--preview docs/barrow/logo.png]

Output: romdisk/tex/logo.tex = the .dmf texture header (u16 w, h, format 2 = ARGB4444, filter 1; u32 bytes; 20 zero
bytes) + 256x64 linear texels (top row first), as romdisk/tex/fx.tex. A real logo replaces the file (same format).
Deterministic: --check rebuilds in memory and compares.
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'vs1art'))
import pvrtex  # noqa: E402

W, H = 256, 64
FONT = os.path.join(ROOT, 'assets', 'fonts', 'HomeVideo-Regular.ttf')
OUT = os.path.join(ROOT, 'romdisk', 'tex', 'logo.tex')


def text_mask(text, px):
    """the text at `px` em without anti-aliasing, cropped to its ink"""
    f = ImageFont.truetype(FONT, px)
    im = Image.new('L', (W * 2, px * 2), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = '1'
    d.text((4, 2), text, font=f, fill=255)
    a = np.asarray(im) > 127
    ys, xs = np.nonzero(a)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy > r * r + 1:
                continue
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def build():
    title = text_mask('PROJECT HYMN', 30)
    sub = text_mask('THE RESTLESS DEAD', 20)
    rgb = np.zeros((H, W, 3), np.float64)
    ink = np.zeros((H, W), bool)
    sub_ink = np.zeros((H, W), bool)
    ty = 6
    tx = (W - title.shape[1]) // 2
    ink[ty:ty + title.shape[0], tx:tx + title.shape[1]] = title
    sy = ty + title.shape[0] + 9
    sx = (W - sub.shape[1]) // 2
    sub_ink[sy:sy + sub.shape[0], sx:sx + sub.shape[1]] = sub
    # the title: gold at the top to ember at the bottom; the subtitle a pale lilac (the barrow's colour)
    yy = (np.arange(H)[:, None] - ty) / max(1, title.shape[0] - 1)
    top, bot = np.array([255, 236, 160]), np.array([214, 120, 56])
    grad = top[None, None, :] * (1 - np.clip(yy, 0, 1))[..., None] + bot[None, None, :] * np.clip(yy, 0, 1)[..., None]
    rgb[ink] = np.broadcast_to(grad, (H, W, 3))[ink]
    rgb[sub_ink] = (222, 204, 244)
    # a 2 px outline around both, then a soft shadow (down-right) under everything
    both = ink | sub_ink
    outline = dilate(both, 2) & ~both
    rgb[outline] = (30, 16, 22)
    body = both | outline
    shadow = np.roll(np.roll(dilate(body, 1), 3, 0), 3, 1) & ~body
    alpha = np.where(body, 255, np.where(shadow, 110, 0))
    rgb[shadow] = (0, 0, 0)
    rgba = np.concatenate([np.clip(rgb, 0, 255), alpha[..., None]], -1).round().astype(np.uint8)
    return rgba


def tex_bytes(rgba):
    t = pvrtex.pack(rgba, pvrtex.FMT_ARGB4444)
    return struct.pack('<4HI20s', W, H, 2, 1, W * H * 2, b'\0' * 20) + t.astype('<u2').tobytes()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--preview')
    a = ap.parse_args()
    rgba = build()
    data = tex_bytes(rgba)
    if a.preview:
        bg = np.zeros((H, W, 3)) + (40, 52, 44)
        al = rgba[..., 3:4] / 255.0
        Image.fromarray((bg * (1 - al) + rgba[..., :3] * al).astype(np.uint8)).resize((W * 2, H * 2), Image.NEAREST).save(a.preview)
    if a.check:
        same = os.path.exists(OUT) and open(OUT, 'rb').read() == data
        print('%s %s' % ('ok  ' if same else 'DIFF', OUT))
        return 0 if same else 1
    open(OUT, 'wb').write(data)
    print('wrote %s (%dx%d ARGB4444, %d bytes)' % (OUT, W, H, len(data)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
