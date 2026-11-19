#!/usr/bin/env python3
"""make_ui_atlas.py - the UI atlas (docs/UI_ART.md): the anti-aliased UI font in two sizes and the HUD / battle icons,
one 512x256 ARGB4444 texture + metrics in romdisk/ui/ui.uia. Replaces, in the 3D game, the 5x7 bitmap font
(romdisk/font.png) and the 16x16 pixel icons (romdisk/ui_icons.png) that came from the 2D battle engine.

    python3 tools/ui/make_ui_atlas.py [--check] [--preview docs/ui/ui_atlas.png]

Font: Poppins Bold (assets/fonts/Poppins-Bold.ttf, SIL OFL 1.1), rendered at 4x with anti-aliasing, a dark outline and a
soft drop shadow baked in (one sprite per glyph at run time: the old outlined text drew each glyph five times), then
box-filtered to 1x. Two sizes: S (em 20 px, cap 14: the HUD) and L (em 40 px, cap 28: damage numbers, banners, titles),
ASCII 32..95 (lower case is drawn with the capitals, as the 5x7 font did).

Icons: the sword and the shield are the game's own weapon models (romdisk/models/w_sword.dmf, w_shield.dmf, with the
knight's texture) rendered by the preview rasteriser (tools/battle3d/raster.c) at 4x; the flame, the potion, the pad
buttons, the lock and the token are painted procedurally with the same light (upper left) and shading. Every icon gets
the same dark outline and drop shadow; colours are dithered (4x4 ordered) before the 4-bit quantisation.

File (little-endian): header 32 B {'UIA1', u16 version 2, w, h, nfonts 2, nglyphs 64, nicons, u32 off_data,
texel_bytes, packed_bytes, 4 pad}; nfonts x 16 B {u16 em, cap, line, outline, 8 pad}; nfonts x nglyphs x 12 B {u16 x, y;
u8 w, h; i8 ox, oy (px from the pen and the cap line to the glyph box); u16 advance in 1/16 px; u16 pad}; nicons x 8 B
{u16 x, y, w, h}; at off_data (32-aligned): the w x h ARGB4444 texels (top row first), packed with LZ16 (below: ~48 KB
instead of 256 KiB on the romdisk, which lives in RAM). Deterministic: --check rebuilds in memory and compares.

LZ16 (unpacked by src/uitext.cpp, dependency-free): a stream of tokens over 16-bit words. t < 128: t + 1 literal words
follow; t >= 128: copy t - 126 words from `d` words back (u16 d follows, 1..65535).
"""
import argparse, os, struct, sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'vs1art'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
import pvrtex  # noqa: E402

FONT = os.path.join(ROOT, 'assets', 'fonts', 'Poppins-Bold.ttf')
OUT = os.path.join(ROOT, 'romdisk', 'ui', 'ui.uia')
AW, AH = 512, 256
SS = 4                                   # supersampling
FIRST, NGLYPH = 32, 64                   # ASCII 32..95
OUTLINE_RGB = np.array([14, 10, 22]) / 255.0
L_CHARS = " !%'+,-./0123456789:?ABCDEFGHIJKLMNOPQRSTUVWXYZ"   # L: numbers, banners and titles only (atlas space)
FONTS = [dict(em=20, outline=2.0, shadow=(1, 2), shadow_a=0.55, chars=None),       # S: ASCII 32..95
         dict(em=40, outline=3.0, shadow=(2, 3), shadow_a=0.55, chars=L_CHARS)]    # L
# icons: (name, cell px, outline px) - order == enum UiIcon in src/uitext.h
ICONS = [('sword', 48, 2.5), ('flame', 48, 2.5), ('shield', 48, 2.5), ('potion', 48, 2.5),
         ('pad_a', 24, 1.5), ('pad_b', 24, 1.5), ('pad_x', 24, 1.5), ('pad_y', 24, 1.5), ('pad_start', 24, 1.5),
         ('pad_left', 24, 1.5), ('pad_right', 24, 1.5), ('pad_up', 24, 1.5), ('pad_down', 24, 1.5),
         ('lock', 24, 1.5), ('potion_s', 24, 1.5), ('token', 24, 1.5)]
LIGHT = np.array([-0.45, -0.65, 0.62])
LIGHT = LIGHT / np.linalg.norm(LIGHT)    # towards the light, screen space (x right, y down, z out)


# ---------------------------------------------------------------------------- helpers
def disk(r):
    n = int(np.ceil(r))
    y, x = np.mgrid[-n:n + 1, -n:n + 1]
    return (x * x + y * y) <= r * r + 0.25


def decorate(fill_rgb, fill_a, outline, shadow, shadow_a):
    """premultiplied composite at SS: shadow (black), outline (dark), the fill on top. fill_rgb: a colour (3,) or a
    straight-colour image (h, w, 3). Returns premultiplied rgb and alpha (SS scale)."""
    o = ndimage.grey_dilation(fill_a, footprint=disk(outline * SS))
    s = ndimage.uniform_filter(np.roll(np.roll(o, shadow[1] * SS, 0), shadow[0] * SS, 1), size=SS) * shadow_a
    a_f = fill_a
    a_o = o * (1 - a_f)
    a_s = s * (1 - a_f) * (1 - o)
    rgb = fill_rgb * a_f[..., None] + OUTLINE_RGB * a_o[..., None]      # the shadow is black
    return rgb, a_f + a_o + a_s


def downsample(rgb, a):
    """box filter SS x SS on premultiplied colour; un-premultiply"""
    h, w = a.shape[0] // SS, a.shape[1] // SS
    a1 = a[:h * SS, :w * SS].reshape(h, SS, w, SS).mean((1, 3))
    c1 = rgb[:h * SS, :w * SS].reshape(h, SS, w, SS, 3).mean((1, 3))
    c1 = np.where(a1[..., None] > 1e-6, c1 / np.maximum(a1[..., None], 1e-6), 0)
    return np.clip(c1, 0, 1), np.clip(a1, 0, 1)


BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5 + 1 / 32.0


def to_rgba8(rgb, a, dither):
    if dither:
        h, w = a.shape
        d = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w][..., None] / 15.0
        rgb = np.clip(rgb + d, 0, 1)
    return np.dstack([np.round(rgb * 255), np.round(a * 255)]).astype(np.uint8)


# ---------------------------------------------------------------------------- font
def glyphs(spec):
    em, ol = spec['em'], spec['outline']
    f = ImageFont.truetype(FONT, em * SS)
    cap = -f.getbbox('H', anchor='ls')[1] / SS                   # px above the baseline
    out = []
    pad = int((ol + 4) * SS)
    for code in range(FIRST, FIRST + NGLYPH):
        ch = chr(code)
        adv = f.getlength(ch) / SS
        x0, y0, x1, y1 = f.getbbox(ch, anchor='ls')
        if spec['chars'] is not None and ch not in spec['chars']:
            out.append(dict(img=None, ox=0, oy=0, adv=0.0))          # not in this size: advance 0 = absent
            continue
        if x1 <= x0 or y1 <= y0:
            out.append(dict(img=None, ox=0, oy=0, adv=adv))
            continue
        # canvas: the pen at (px, base), both multiples of SS so the 1x grid lines up with the pen and the baseline
        px = (pad - min(0, x0) + SS - 1) // SS * SS
        base = (pad - y0 + SS - 1) // SS * SS
        W = ((px + x1 + pad) // SS + 1) * SS
        H = ((base + y1 + pad) // SS + 1) * SS
        im = Image.new('L', (W, H), 0)
        ImageDraw.Draw(im).text((px, base), ch, font=f, fill=255, anchor='ls')
        m = np.asarray(im, np.float64) / 255.0
        rgb, a = decorate(np.ones(3), m, ol, spec['shadow'], spec['shadow_a'])
        c1, a1 = downsample(rgb, a)
        ys, xs = np.nonzero(a1 > 1.5 / 255)
        by0, by1, bx0, bx1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        img = to_rgba8(c1[by0:by1, bx0:bx1], a1[by0:by1, bx0:bx1], False)
        out.append(dict(img=img, ox=bx0 - px // SS, oy=by0 - (base // SS - int(round(cap))), adv=adv))
    return out, int(round(cap))


# ---------------------------------------------------------------------------- icons
def shade_sphere(nx, ny, inside, base, spec=0.55, amb=0.45):
    nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
    ndl = np.clip(nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2], 0, 1)
    h = np.array([0, 0, 1.0]) + LIGHT
    h /= np.linalg.norm(h)
    sp = np.clip(nx * h[0] + ny * h[1] + nz * h[2], 0, 1) ** 24 * spec
    col = np.asarray(base)[None, None, :] * (amb + (1 - amb) * ndl)[..., None] + sp[..., None]
    return np.clip(col, 0, 1) * inside[..., None]


def grid(n):
    y, x = np.mgrid[0:n, 0:n]
    return (x + 0.5) / n, (y + 0.5) / n          # 0..1


def mask_poly(n, pts):
    im = Image.new('L', (n, n), 0)
    ImageDraw.Draw(im).polygon([(x * n, y * n) for x, y in pts], fill=255)
    return np.asarray(im, np.float64) / 255.0


def mask_ellipse(n, box):
    im = Image.new('L', (n, n), 0)
    ImageDraw.Draw(im).ellipse([v * n for v in box], fill=255)
    return np.asarray(im, np.float64) / 255.0


def mask_rrect(n, box, r):
    im = Image.new('L', (n, n), 0)
    ImageDraw.Draw(im).rounded_rectangle([v * n for v in box], radius=r * n, fill=255)
    return np.asarray(im, np.float64) / 255.0


def over(dst_rgb, dst_a, rgb, a):
    """straight-alpha layer `rgb, a` over the premultiplied canvas"""
    return dst_rgb * (1 - a[..., None]) + rgb * a[..., None], dst_a * (1 - a) + a


def model_icon(name, n, yaw, roll, pitch=0.0):
    """a weapon .dmf rendered by the preview rasteriser (the knight's texture), cropped to its ink, fitted to n x n"""
    import dmfio, preview, raster
    knight = dmfio.read_dmf(os.path.join(ROOT, 'romdisk', 'models', 'knight.dmf'))
    tex = preview.load_tex(knight)
    m = dmfio.read_dmf(os.path.join(ROOT, 'romdisk', 'models', name + '.dmf'))
    R = 768
    c = raster.Canvas(R, R, 0x000000)
    cen = (m['bmin'] + m['bmax']) / 2
    cam = raster.Camera((cen[0], cen[1], 6.0), tuple(cen), 24, R, R)
    W = preview.trs_y((0, 0, 0), yaw)
    cz, sz = np.cos(roll), np.sin(roll)
    Rz = np.eye(4); Rz[:2, :2] = [[cz, -sz], [sz, cz]]
    cp, sp = np.cos(pitch), np.sin(pitch)
    Rx = np.eye(4); Rx[1:3, 1:3] = [[cp, -sp], [sp, cp]]
    T = np.eye(4); T[:3, 3] = cen
    Ti = np.eye(4); Ti[:3, 3] = -cen
    world = T @ Rz @ Rx @ W @ Ti
    light = np.array([-0.45, 0.65, 0.62]); light /= np.linalg.norm(light)        # world: y up
    preview.draw_model(c, cam, m, world, None, holder_tex=tex, light=light, ambient=0.62, diffuse=0.6, cull=False)
    a = (c.depth > 0).astype(np.float64)
    rgb = np.clip(c.color / 255.0, 0, 1)
    ys, xs = np.nonzero(a)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    side = max(y1 - y0, x1 - x0)
    cy, cx = (y0 + y1) // 2, (x0 + x1) // 2
    half = side // 2 + 2
    crop = lambda arr: arr[max(0, cy - half):cy + half, max(0, cx - half):cx + half]
    rgb, a = crop(rgb), crop(a)
    im = Image.fromarray(np.dstack([np.round(rgb * 255), np.round(a * 255)]).astype(np.uint8), 'RGBA')
    im = im.resize((n, n), Image.LANCZOS)
    arr = np.asarray(im, np.float64) / 255.0
    return arr[..., :3], arr[..., 3]


def icon_flame(n):
    x, y = grid(n)

    def tear(cx, cy, w, h, lean):
        # a flame tongue: round at the bottom, pointed at the top, leaning
        t = np.linspace(0, 2 * np.pi, 200)
        px = np.sin(t) * np.abs(np.sin(t / 2)) ** 1.2 * w
        py = -np.cos(t) * h
        px = px + lean * (py / h - 1) * 0.5 * w * -1
        return mask_poly(n, list(zip(cx + px, cy + py)))
    outer = np.maximum.reduce([tear(0.5, 0.56, 0.36, 0.40, 0.0), tear(0.33, 0.66, 0.17, 0.27, 0.5),
                               tear(0.68, 0.64, 0.16, 0.25, -0.5)])
    mid = tear(0.5, 0.66, 0.24, 0.28, 0.1)
    core = tear(0.5, 0.74, 0.12, 0.16, 0.0)
    g = np.clip((y - 0.15) / 0.75, 0, 1)[..., None]
    c_out = np.array([0.86, 0.16, 0.06]) * (1 - g) + np.array([1.0, 0.48, 0.08]) * g
    rgb, a = np.zeros((n, n, 3)), np.zeros((n, n))
    rgb, a = over(rgb, a, c_out, outer)
    rgb, a = over(rgb, a, np.array([1.0, 0.72, 0.12]) * (1 - g * 0.3) + np.array([1.0, 0.86, 0.3]) * g * 0.3, mid)
    rgb, a = over(rgb, a, np.array([1.0, 0.97, 0.78]), core)
    return np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), a


def icon_potion(n):
    x, y = grid(n)
    cx, cy, r = 0.5, 0.63, 0.31
    dx, dy = (x - cx) / r, (y - cy) / r
    body = mask_ellipse(n, (cx - r, cy - r, cx + r, cy + r))
    neck = mask_rrect(n, (0.41, 0.17, 0.59, 0.40), 0.03)
    lip = mask_rrect(n, (0.37, 0.14, 0.63, 0.22), 0.04)
    cork = mask_rrect(n, (0.42, 0.03, 0.58, 0.17), 0.03)
    liquid = body * (y > cy - 0.08)
    rgb, a = np.zeros((n, n, 3)), np.zeros((n, n))
    glass = shade_sphere(dx, dy, np.ones_like(dx), [0.70, 0.82, 0.88], spec=0.9, amb=0.55)
    rgb, a = over(rgb, a, glass, body * 0.55)
    rgb, a = over(rgb, a, shade_sphere(dx * 0.95, dy * 0.95, np.ones_like(dx), [0.86, 0.10, 0.22], spec=0.7, amb=0.5), liquid)
    surf = mask_ellipse(n, (cx - 0.27, cy - 0.115, cx + 0.27, cy - 0.045)) * body
    rgb, a = over(rgb, a, np.array([1.0, 0.45, 0.5]), surf * 0.9)
    nx = np.clip((x - 0.5) / 0.09, -1, 1)
    cyl = lambda base: shade_sphere(nx, np.zeros_like(nx), np.ones_like(nx), base, spec=0.6, amb=0.55)
    rgb, a = over(rgb, a, cyl([0.72, 0.84, 0.9]), np.maximum(neck, lip) * 0.85)
    rgb, a = over(rgb, a, cyl([0.62, 0.40, 0.22]), cork)
    hl = mask_ellipse(n, (0.30, 0.48, 0.40, 0.64))                     # the glass's highlight
    rgb, a = over(rgb, a, np.array([1.0, 1.0, 1.0]), hl * 0.55)
    return np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), a


def letter_mask(n, ch, size, dy=0.0):
    f = ImageFont.truetype(FONT, int(size * n))
    im = Image.new('L', (n, n), 0)
    ImageDraw.Draw(im).text((n / 2, n / 2 + dy * n), ch, font=f, fill=255, anchor='mm')
    return np.asarray(im, np.float64) / 255.0


def icon_button(n, base, ch):
    x, y = grid(n)
    r = 0.44
    dx, dy = (x - 0.5) / r, (y - 0.5) / r
    disc = mask_ellipse(n, (0.5 - r, 0.5 - r, 0.5 + r, 0.5 + r))
    rgb, a = np.zeros((n, n, 3)), np.zeros((n, n))
    rgb, a = over(rgb, a, shade_sphere(dx * 0.8, dy * 0.8, np.ones_like(dx), base, spec=0.7, amb=0.6), disc)
    if ch:
        lm = letter_mask(n, ch, 0.62, 0.03)
        sh = np.roll(np.roll(lm, n // 40 + 1, 0), n // 60, 1)
        rgb, a = over(rgb, a, np.array([0, 0, 0]), sh * 0.45 * disc)
        rgb, a = over(rgb, a, np.array([1.0, 1.0, 1.0]), lm * disc)
    return np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), a


def icon_start(n):
    rgb, a = icon_button(n, [0.55, 0.56, 0.62], '')
    tri = mask_poly(n, [(0.40, 0.30), (0.40, 0.70), (0.72, 0.50)])
    rgb = rgb * a[..., None]
    rgb, a = over(rgb, a, np.array([1.0, 1.0, 1.0]), tri)
    return np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), a


def icon_dpad(n, d):
    """the d-pad: a dark cross, the pressed direction's arm lit gold with a dark arrow in it"""
    x, y = grid(n)
    arm = 0.30
    cross = np.maximum(mask_rrect(n, (0.5 - arm / 2, 0.04, 0.5 + arm / 2, 0.96), 0.05),
                       mask_rrect(n, (0.04, 0.5 - arm / 2, 0.96, 0.5 + arm / 2), 0.05))
    rgb, a = np.zeros((n, n, 3)), np.zeros((n, n))
    rgb, a = over(rgb, a, np.array([0.30, 0.32, 0.38]) * (0.8 + 0.2 * (1 - y))[..., None], cross)
    arms = {'left': (0.04, 0.5 - arm / 2, 0.48, 0.5 + arm / 2), 'right': (0.52, 0.5 - arm / 2, 0.96, 0.5 + arm / 2),
            'up': (0.5 - arm / 2, 0.04, 0.5 + arm / 2, 0.48), 'down': (0.5 - arm / 2, 0.52, 0.5 + arm / 2, 0.96)}
    lit = mask_rrect(n, arms[d], 0.05)
    rgb, a = over(rgb, a, np.array([1.0, 0.84, 0.36]) * (0.85 + 0.15 * (1 - y))[..., None], lit)
    tips = {'left': [(0.12, 0.5), (0.32, 0.37), (0.32, 0.63)], 'right': [(0.88, 0.5), (0.68, 0.37), (0.68, 0.63)],
            'up': [(0.5, 0.12), (0.37, 0.32), (0.63, 0.32)], 'down': [(0.5, 0.88), (0.37, 0.68), (0.63, 0.68)]}
    rgb, a = over(rgb, a, np.array([0.20, 0.12, 0.06]), mask_poly(n, tips[d]))
    return np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), a


def icon_lock(n):
    x, y = grid(n)
    body = mask_rrect(n, (0.2, 0.44, 0.8, 0.92), 0.08)
    ring = mask_ellipse(n, (0.30, 0.10, 0.70, 0.62)) * (1 - mask_ellipse(n, (0.40, 0.20, 0.60, 0.52))) * (y < 0.48)
    rgb, a = np.zeros((n, n, 3)), np.zeros((n, n))
    rgb, a = over(rgb, a, np.array([0.78, 0.80, 0.86]) * (0.7 + 0.3 * (1 - y))[..., None], ring)
    nx = np.clip((x - 0.5) / 0.3, -1, 1)
    rgb, a = over(rgb, a, shade_sphere(nx * 0.7, np.zeros_like(nx) - 0.2, np.ones_like(nx), [0.95, 0.72, 0.22], spec=0.6), body)
    hole = np.maximum(mask_ellipse(n, (0.44, 0.56, 0.56, 0.68)), mask_poly(n, [(0.47, 0.62), (0.53, 0.62), (0.55, 0.80), (0.45, 0.80)]))
    rgb, a = over(rgb, a, np.array([0.18, 0.10, 0.06]), hole)
    return np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), a


def icon_token(n):
    x, y = grid(n)
    r = 0.42
    dx, dy = (x - 0.5) / r, (y - 0.5) / r
    disc = mask_ellipse(n, (0.5 - r, 0.5 - r, 0.5 + r, 0.5 + r))
    rgb, a = np.zeros((n, n, 3)), np.zeros((n, n))
    rgb, a = over(rgb, a, shade_sphere(dx * 0.6, dy * 0.6, np.ones_like(dx), [0.98, 0.76, 0.26], spec=0.8, amb=0.55), disc)
    inner = mask_ellipse(n, (0.5 - r * 0.72, 0.5 - r * 0.72, 0.5 + r * 0.72, 0.5 + r * 0.72))
    rim = disc * (1 - inner)
    rgb, a = over(rgb, a, np.array([0.74, 0.50, 0.12]), rim * 0.6)
    t = np.linspace(-np.pi / 2, 1.5 * np.pi, 11)[:-1]
    star = [(0.5 + np.cos(v) * (0.22 if k % 2 == 0 else 0.09), 0.5 + np.sin(v) * (0.22 if k % 2 == 0 else 0.09)) for k, v in enumerate(t)]
    rgb, a = over(rgb, a, np.array([1.0, 0.96, 0.78]), mask_poly(n, star))
    return np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), a


def medallion(n, art):
    """the battle commands: the item on a dark round medallion with a light rim (readable over any 3D scene)"""
    x, y = grid(n)
    r = 0.48
    disc = mask_ellipse(n, (0.5 - r, 0.5 - r, 0.5 + r, 0.5 + r))
    inner = mask_ellipse(n, (0.5 - r + 0.05, 0.5 - r + 0.05, 0.5 + r - 0.05, 0.5 + r - 0.05))
    g = np.clip(y, 0, 1)[..., None]
    face = np.array([0.20, 0.24, 0.38]) * (1 - g) + np.array([0.08, 0.09, 0.16]) * g
    rim = np.array([0.78, 0.70, 0.50]) * (1 - g * 0.45)
    rgb, a = np.zeros((n, n, 3)), np.zeros((n, n))
    rgb, a = over(rgb, a, rim, disc)
    rgb, a = over(rgb, a, face, inner)
    k = int(round(n * 0.80))                                   # the item at 80 %, centred
    o = (n - k) // 2
    art_rgb, art_a = art(k)
    big_rgb, big_a = np.zeros((n, n, 3)), np.zeros((n, n))
    big_rgb[o:o + k, o:o + k] = art_rgb
    big_a[o:o + k, o:o + k] = art_a
    rgb, a = over(rgb, a, big_rgb, big_a)
    return np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), a


def icon_art(name, n):
    if name == 'sword':
        return medallion(n, lambda k: model_icon('w_sword', k, yaw=0.0, roll=-0.785))
    if name == 'shield':
        return medallion(n, lambda k: model_icon('w_shield', k, yaw=0.30, roll=0.0, pitch=-0.12))
    if name == 'flame':
        return medallion(n, icon_flame)
    if name == 'potion':
        return medallion(n, icon_potion)
    if name == 'potion_s':
        return icon_potion(n)
    if name.startswith('pad_') and name[4:] in ('a', 'b', 'x', 'y'):
        col = {'a': [0.86, 0.18, 0.18], 'b': [0.20, 0.44, 0.90], 'x': [0.95, 0.74, 0.14], 'y': [0.22, 0.66, 0.30]}[name[4:]]
        return icon_button(n, col, name[4:].upper())
    if name == 'pad_start':
        return icon_start(n)
    if name.startswith('pad_'):
        return icon_dpad(n, name[4:])
    if name == 'lock':
        return icon_lock(n)
    if name == 'token':
        return icon_token(n)
    raise ValueError(name)


def icons():
    out = []
    for name, cell, ol in ICONS:
        inner = cell - 2 * int(np.ceil(ol)) - 2          # the art inside the outline + shadow
        art_rgb, art_a = icon_art(name, inner * SS)
        pad = (cell * SS - inner * SS) // 2
        rgb0 = np.zeros((cell * SS, cell * SS, 3)); a0 = np.zeros((cell * SS, cell * SS))
        rgb0[pad:pad + inner * SS, pad:pad + inner * SS] = art_rgb
        a0[pad:pad + inner * SS, pad:pad + inner * SS] = art_a
        rgb, a = decorate(rgb0, a0, ol, (1, 1), 0.45)     # the art (straight colour) over its outline and shadow
        c1, a1 = downsample(rgb, a)
        out.append(dict(name=name, img=to_rgba8(c1, a1, True)))
    return out


# ---------------------------------------------------------------------------- LZ16
def lz16_pack(words):
    """greedy LZ77 over u16 words (the format above); the last 16 positions of each 2-word key are tried"""
    n = len(words)
    out, lit, table = bytearray(), [], {}
    i = 0

    def flush():
        while lit:
            k = min(128, len(lit))
            out.append(k - 1)
            out.extend(struct.pack('<%dH' % k, *lit[:k]))
            del lit[:k]
    while i < n:
        best_l, best_d = 0, 0
        if i + 1 < n:
            for p in table.get((words[i], words[i + 1]), [])[-16:]:
                d = i - p
                if d > 65535:
                    continue
                ln = 0
                while i + ln < n and ln < 129 and words[p + ln] == words[i + ln]:
                    ln += 1
                if ln > best_l:
                    best_l, best_d = ln, d
        if best_l >= 2:
            flush()
            out.append(128 + best_l - 2)
            out.extend(struct.pack('<H', best_d))
            for k in range(best_l):
                if i + k + 1 < n:
                    table.setdefault((words[i + k], words[i + k + 1]), []).append(i + k)
            i += best_l
        else:
            if i + 1 < n:
                table.setdefault((words[i], words[i + 1]), []).append(i)
            lit.append(words[i])
            i += 1
    flush()
    return bytes(out)


def lz16_unpack(b):
    out, i = [], 0
    while i < len(b):
        t = b[i]
        i += 1
        if t < 128:
            out.extend(struct.unpack_from('<%dH' % (t + 1), b, i))
            i += 2 * (t + 1)
        else:
            d = struct.unpack_from('<H', b, i)[0]
            i += 2
            for _ in range(t - 126):
                out.append(out[-d])
    return out


# ---------------------------------------------------------------------------- atlas
def pack(items):
    """shelf packing, tallest first; 2 px transparent gaps (bilinear filtering reads the neighbours)"""
    order = sorted(range(len(items)), key=lambda i: (-items[i].shape[0], -items[i].shape[1], i))
    pos = [None] * len(items)
    x, y, shelf = 0, 0, 0
    for i in order:
        h, w = items[i].shape[:2]
        if x + w + 2 > AW:
            x, y, shelf = 0, y + shelf + 2, 0
        if y + h + 2 > AH:
            raise SystemExit('the UI atlas is full (%dx%d)' % (AW, AH))
        pos[i] = (x + 1, y + 1)
        x += w + 2
        shelf = max(shelf, h)
    return pos


def build():
    fonts = [glyphs(s) for s in FONTS]
    ics = icons()
    items, refs = [], []
    for fi, (gl, _) in enumerate(fonts):
        for gi, g in enumerate(gl):
            if g['img'] is not None:
                items.append(g['img']); refs.append(('g', fi, gi))
    for ii, ic in enumerate(ics):
        items.append(ic['img']); refs.append(('i', ii, 0))
    pos = pack(items)
    atlas = np.zeros((AH, AW, 4), np.uint8)
    for img, (x, y) in zip(items, pos):
        atlas[y:y + img.shape[0], x:x + img.shape[1]] = img
    where = {r: p for r, p in zip(refs, pos)}
    head_fonts = b''
    glyph_tab = b''
    for fi, (gl, cap) in enumerate(fonts):
        s = FONTS[fi]
        head_fonts += struct.pack('<4H8s', s['em'], cap, int(round(s['em'] * 1.15)), int(np.ceil(s['outline'])), b'\0' * 8)
        for gi, g in enumerate(gl):
            adv = int(round(g['adv'] * 16))
            if g['img'] is None:
                glyph_tab += struct.pack('<2H2B2bHH', 0, 0, 0, 0, 0, 0, adv, 0)
            else:
                x, y = where[('g', fi, gi)]
                glyph_tab += struct.pack('<2H2B2bHH', x, y, g['img'].shape[1], g['img'].shape[0], g['ox'], g['oy'], adv, 0)
    icon_tab = b''
    for ii, ic in enumerate(ics):
        x, y = where[('i', ii, 0)]
        icon_tab += struct.pack('<4H', x, y, ic['img'].shape[1], ic['img'].shape[0])
    body = head_fonts + glyph_tab + icon_tab
    off = (32 + len(body) + 31) // 32 * 32
    words = [int(v) for v in pvrtex.pack(atlas, pvrtex.FMT_ARGB4444).reshape(-1)]
    packed = lz16_pack(words)
    assert lz16_unpack(packed) == words
    head = struct.pack('<4s6H3I4s', b'UIA1', 2, AW, AH, len(FONTS), NGLYPH, len(ICONS), off, 2 * len(words), len(packed), b'\0' * 4)
    data = head + body + b'\0' * (off - 32 - len(body)) + packed
    return data, atlas, fonts, ics, where


def preview(path, atlas, fonts, where):
    """the atlas over a checkerboard, and text samples at their sizes over a game-like background"""
    dec = pvrtex.unpack(pvrtex.pack(atlas, pvrtex.FMT_ARGB4444), pvrtex.FMT_ARGB4444).astype(np.float64) / 255
    chk = ((np.indices((AH, AW)).sum(0) // 8) % 2)[..., None] * 0.12 + 0.22
    a = dec[..., 3:4]
    top = dec[..., :3] * a + chk * (1 - a)
    im = Image.fromarray(np.round(top * 255).astype(np.uint8)).resize((AW * 2, AH * 2), Image.NEAREST)
    im.save(path)
    print('wrote', path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--preview')
    a = ap.parse_args()
    data, atlas, fonts, ics, where = build()
    if a.preview:
        preview(a.preview, atlas, fonts, where)
    if a.check:
        same = os.path.exists(OUT) and open(OUT, 'rb').read() == data
        print('%s %s' % ('ok  ' if same else 'DIFF', OUT))
        return 0 if same else 1
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, 'wb').write(data)
    used = int((atlas[..., 3] > 0).sum())
    print('wrote %s (%d bytes; %dx%d ARGB4444, %d%% of the texels used; fonts S cap %d, L cap %d; %d icons)' % (
        OUT, len(data), AW, AH, 100 * used // (AW * AH), fonts[0][1], fonts[1][1], len(ics)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
