"""make_fx.py - procedural textures of the 3D battle (no art needed; slice 3D-4 asset N4 stand-ins).

  romdisk/tex/fx.tex   128x128 ARGB4444 FX atlas (32 KiB VRAM), raw: 32-byte header (.dmf texture
                       header: w, h, format=2, filter=1, byte count) + texels, top row first.
                       White shapes with alpha; the game tints them with the vertex colour.
  romdisk/tex/dirt.png 128x128 tileable trampled dirt for the arena clearing (RGB565 on upload),
                       colourised from the village kit's CC0 terrain noise (T_Noise_Terrain.png).
  docs/battle3d/fx_atlas.png  preview (cells on a dark background, labelled)

Cells (pixels, x0 y0 w h) - src/battle3d_fx.h FX_CELL must match:
  RUNE   0   0 64 64   rune ring (selection decal)       RING  64  0 64 64   plain ring (decals, shockwaves)
  GLOW   0  64 32 32   soft round glow                   STAR  32 64 32 32   4-point spark
  FLAME 64  64 32 32   flame tongue                      SMOKE 96 64 32 32   soft puff
  ARROW  0  96 32 32   down-pointing chevron (target)    BUBBLE 32 96 32 32  shield bubble (rim-lit disc)
  SHARD 64  96 32 32   bone shard / splinter             DOT  96 96 32 32    small hard dot (embers, dust)
Deterministic (fixed seeds); byte-for-byte reproducible.
"""
import os
import struct
import sys
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
KAYKIT_VILLAGE = os.environ.get('VILLAGE', os.path.join(ROOT, '..', 'Village'))

CELLS = {
    'RUNE': (0, 0, 64, 64), 'RING': (64, 0, 64, 64),
    'GLOW': (0, 64, 32, 32), 'STAR': (32, 64, 32, 32), 'FLAME': (64, 64, 32, 32), 'SMOKE': (96, 64, 32, 32),
    'ARROW': (0, 96, 32, 32), 'BUBBLE': (32, 96, 32, 32), 'SHARD': (64, 96, 32, 32), 'DOT': (96, 96, 32, 32),
}


def grid(n):
    c = (np.arange(n) + 0.5) / n * 2 - 1
    return np.meshgrid(c, c)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def cell_rune(n):
    x, y = grid(n)
    r = np.hypot(x, y); a = np.arctan2(y, x)
    ring = np.exp(-((r - 0.86) / 0.045) ** 2) + 0.7 * np.exp(-((r - 0.66) / 0.03) ** 2)
    # 12 rune ticks between the two rings, alternating long/short
    k = 12
    seg = (a / (2 * np.pi) * k) % 1.0
    tick = (np.abs(seg - 0.5) < 0.06) * ((r > 0.69) & (r < 0.83))
    dots = (np.abs(((a / (2 * np.pi) * k + 0.5) % 1.0) - 0.5) < 0.05) * (np.abs(r - 0.76) < 0.035)
    alpha = np.clip(ring + 0.9 * tick + 0.8 * dots, 0, 1)
    return np.ones_like(alpha), alpha


def cell_ring(n):
    x, y = grid(n)
    r = np.hypot(x, y)
    alpha = np.clip(np.exp(-((r - 0.82) / 0.09) ** 2) + 0.25 * smoothstep(0.85, 0.3, r) * (r < 0.85), 0, 1)
    return np.ones_like(alpha), alpha


def cell_glow(n):
    x, y = grid(n)
    r = np.hypot(x, y)
    alpha = np.clip(np.exp(-(r / 0.45) ** 2) * 1.05, 0, 1) * (r < 1)
    return np.ones_like(alpha), alpha


def cell_star(n):
    x, y = grid(n)
    r = np.hypot(x, y)
    arms = np.maximum(np.exp(-np.abs(x) / 0.06) * np.exp(-np.abs(y) / 0.55), np.exp(-np.abs(y) / 0.06) * np.exp(-np.abs(x) / 0.55))
    alpha = np.clip(arms + np.exp(-(r / 0.18) ** 2), 0, 1)
    return np.ones_like(alpha), alpha


def cell_flame(n):
    x, y = grid(n)
    # teardrop: wide at the bottom (y = +1 is the bottom row), pointed at the top
    yy = (y + 1) / 2                      # 0 top .. 1 bottom
    width = 0.08 + 0.55 * np.sqrt(np.clip(yy - 0.05, 0, 1)) * (1 - smoothstep(0.78, 1.0, yy))
    body = smoothstep(width, width * 0.35, np.abs(x)) * smoothstep(0.02, 0.25, yy)
    core = np.exp(-((x / 0.18) ** 2 + ((yy - 0.72) / 0.18) ** 2))
    alpha = np.clip(body * 0.85 + core * 0.6, 0, 1)
    lum = np.clip(0.55 + 0.45 * core + 0.25 * yy, 0, 1)      # brighter core (tinted orange in game)
    return lum, alpha


def cell_smoke(n, seed=7):
    rng = np.random.RandomState(seed)
    x, y = grid(n)
    r = np.hypot(x, y)
    alpha = np.zeros_like(r)
    for _ in range(7):
        cx, cy, s = rng.uniform(-0.35, 0.35), rng.uniform(-0.35, 0.35), rng.uniform(0.28, 0.45)
        alpha = np.maximum(alpha, np.exp(-(((x - cx) ** 2 + (y - cy) ** 2) / s ** 2)))
    alpha *= smoothstep(1.0, 0.7, r)
    return np.full_like(alpha, 0.9), np.clip(alpha * 0.9, 0, 1)


def cell_arrow(n):
    img = Image.new('L', (n * 4, n * 4), 0)
    d = ImageDraw.Draw(img)
    s = n * 4
    d.polygon([(s * 0.15, s * 0.18), (s * 0.85, s * 0.18), (s * 0.5, s * 0.86)], fill=255)
    a = np.asarray(img.resize((n, n), Image.LANCZOS), float) / 255
    # slightly darker rim so it reads on bright ground
    x, y = grid(n)
    return np.clip(0.75 + 0.25 * (1 - y) / 2, 0, 1), a


def cell_bubble(n):
    x, y = grid(n)
    r = np.hypot(x, y)
    rim = np.exp(-((r - 0.9) / 0.08) ** 2)
    fill = 0.22 * (r < 0.92) + 0.25 * np.exp(-(((x + 0.35) ** 2 + (y + 0.35) ** 2) / 0.05))   # highlight
    alpha = np.clip(rim + fill, 0, 1) * (r < 1)
    return np.ones_like(alpha), alpha


def cell_shard(n):
    img = Image.new('L', (n * 4, n * 4), 0)
    d = ImageDraw.Draw(img)
    s = n * 4
    d.polygon([(s * 0.5, s * 0.05), (s * 0.64, s * 0.45), (s * 0.52, s * 0.95), (s * 0.38, s * 0.5)], fill=255)
    a = np.asarray(img.resize((n, n), Image.LANCZOS), float) / 255
    return np.full_like(a, 0.95), a


def cell_dot(n):
    x, y = grid(n)
    r = np.hypot(x, y)
    alpha = smoothstep(0.55, 0.35, r)
    return np.ones_like(alpha), alpha


MAKERS = {'RUNE': cell_rune, 'RING': cell_ring, 'GLOW': cell_glow, 'STAR': cell_star, 'FLAME': cell_flame,
          'SMOKE': cell_smoke, 'ARROW': cell_arrow, 'BUBBLE': cell_bubble, 'SHARD': cell_shard, 'DOT': cell_dot}


def argb4444(lum, alpha):
    l4 = np.clip(np.round(lum * 15), 0, 15).astype(np.uint16)
    a4 = np.clip(np.round(alpha * 15), 0, 15).astype(np.uint16)
    return (a4 << 12) | (l4 << 8) | (l4 << 4) | l4


def build_atlas():
    lum = np.zeros((128, 128)); alpha = np.zeros((128, 128))
    for name, (x0, y0, w, h) in CELLS.items():
        l, a = MAKERS[name](w)
        # keep a 1-texel transparent border so bilinear sampling never bleeds between cells
        a[0, :] = a[-1, :] = a[:, 0] = a[:, -1] = 0
        lum[y0:y0 + h, x0:x0 + w] = l; alpha[y0:y0 + h, x0:x0 + w] = a
    return lum, alpha


def build_dirt():
    src = os.path.join(KAYKIT_VILLAGE, 'Textures', 'T_Noise_Terrain.png')
    n = np.asarray(Image.open(src).convert('L').resize((128, 128), Image.BOX), float) / 255
    rng = np.random.RandomState(11)
    fine = rng.rand(128, 128)
    fine = (fine + np.roll(fine, 1, 0) + np.roll(fine, 1, 1) + np.roll(np.roll(fine, 1, 0), 1, 1)) / 4
    v = np.clip(0.55 * n + 0.25 * fine + 0.2, 0, 1)
    dark, light = np.array([96, 70, 46]), np.array([168, 132, 92])
    rgb = dark[None, None, :] * (1 - v[..., None]) + light[None, None, :] * v[..., None]
    # a few pebbles (tileable: drawn with wrap-around)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    for _ in range(26):
        cx, cy, r = rng.randint(0, 128), rng.randint(0, 128), rng.uniform(1.0, 2.6)
        shade = int(rng.uniform(120, 175))
        for ox in (-128, 0, 128):
            for oy in (-128, 0, 128):
                d.ellipse([cx + ox - r, cy + oy - r * 0.8, cx + ox + r, cy + oy + r * 0.8], fill=(shade, shade - 8, shade - 22))
    return img


def main():
    lum, alpha = build_atlas()
    tex = argb4444(lum, alpha)
    out = os.path.join(ROOT, 'romdisk', 'tex', 'fx.tex')
    with open(out, 'wb') as f:
        f.write(struct.pack('<4HI20s', 128, 128, 2, 1, 128 * 128 * 2, b'\0' * 20))
        f.write(tex.astype('<u2').tobytes())
    build_dirt().save(os.path.join(ROOT, 'romdisk', 'tex', 'dirt.png'), optimize=False)
    # preview
    pv = np.zeros((128, 128, 3))
    bg = np.array([30, 34, 44])
    pv[:] = bg
    a = (tex >> 12 & 15) / 15.0
    l = (tex & 15) / 15.0 * 255
    pv = pv * (1 - a[..., None]) + l[..., None] * a[..., None]
    big = Image.fromarray(pv.astype(np.uint8)).resize((512, 512), Image.NEAREST)
    d = ImageDraw.Draw(big)
    for name, (x0, y0, w, h) in CELLS.items():
        d.rectangle([x0 * 4, y0 * 4, (x0 + w) * 4 - 1, (y0 + h) * 4 - 1], outline=(80, 80, 90))
        d.text((x0 * 4 + 3, y0 * 4 + 2), name, fill=(255, 200, 80))
    os.makedirs(os.path.join(ROOT, 'docs', 'battle3d'), exist_ok=True)
    big.save(os.path.join(ROOT, 'docs', 'battle3d', 'fx_atlas.png'))
    print('wrote romdisk/tex/fx.tex (128x128 ARGB4444), romdisk/tex/dirt.png, docs/battle3d/fx_atlas.png')


if __name__ == '__main__':
    main()
