#!/usr/bin/env python3
"""make_font.py - the dialog font atlas (slice 3D-5, task B1; GDD 7.5 / 16.2 A2).

    python3 tools/dialog/make_font.py [--ttf assets/fonts/HomeVideo-Regular.ttf] [--scale 2] [--spacing ink|mono]

Writes
  romdisk/fonts/dialog.tex   256x128 ARGB4444 (64 KiB VRAM): the .dmf texture header (32 bytes) +
                             linear texels, like romdisk/tex/fx.tex. Top half (v 0..63): glyphs, white
                             with alpha (the game tints them). Bottom half (v 64..127): UI icons in
                             colour: the quest/talk markers, emotes, the advance marker, the choice
                             cursor, the lock and Wren's token (GDD 7.2, 8, 11.1; A4 stand-ins drawn here).
  romdisk/fonts/dialog.fnt   metrics (format below; src/text.cpp and tools/dialog/lint.py read it)
  tools/dialog/reports/dialog_font.json   the same metrics, readable, + the atlas use
  docs/dialog/font_preview.png            the atlas at 2x and sample pages at 1x on the dialog box colour

The supplied font ("Home Video", CC0, decision D1) is a pixel font: every outline point lies on a
50-unit grid of its 1000-unit em, its strokes are 100 units (one design pixel) wide, its advance is
600 (monospaced) and it has capitals only (the lower-case code points carry the capital shapes).
D1: "rasterise it at its native size (or an integer multiple), without anti-aliasing". This tool fills
the outlines itself at exactly `scale` x 10 px per em (one 50-unit step = scale/2 px, so every edge
lands on a pixel boundary): the result is bit-exact and does not depend on a FreeType version.
Identical glyph shapes (a/A ...) share one atlas cell.

--spacing ink (default): per-glyph widths (GDD 7.5), advance = ink width + one design pixel; the
space is 4 design pixels. --spacing mono: the font's own 600-unit cell (x_off = the ink's bearing).

dialog.fnt (little endian):
  header 16 bytes: 'FNT1', u8 version (1), u8 line_pitch, u8 ink_height, u8 first_char (32),
                   u8 num_glyphs (96), u8 num_icons (16), u8 space_advance, u8 shadow (px),
                   u16 tex_w, u16 tex_h
  96 glyphs x 8 bytes: u8 u, v, w, h (ink box in the atlas; w = 0: nothing drawn), u8 advance,
                   i8 x_off (ink left from the pen), i8 y_off (ink top from the line top), u8 pad
  16 icons x 4 bytes: u8 u, v, w, h (v counts from the atlas top; w = 0: unused slot)
"""
import argparse
import json
import os
import struct
import sys

import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
TEX_W, TEX_H = 256, 128
FIRST, NUM = 32, 96
LINE_PITCH = 22          # GDD 7.1 layout: lines at y = 334, 356, 378
SHADOW = 2               # px: one design pixel down-right (drawn by src/text.cpp)

# icon slots (src/text.h TextIcon, same order)
ICONS = ['marker_quest', 'marker_talk', 'emote_excl', 'emote_question', 'emote_dots', 'emote_sweat', 'emote_anger',
         'advance', 'cursor', 'lock', 'token']


def glyph_contours(font, ch):
    from fontTools.pens.recordingPen import RecordingPen
    gs = font.getGlyphSet()
    name = font.getBestCmap().get(ord(ch))
    if name is None:
        return None, 0
    pen = RecordingPen()
    gs[name].draw(pen)
    contours, cur = [], []
    for op, pts in pen.value:
        if op == 'moveTo':
            cur = [pts[0]]
        elif op == 'lineTo':
            cur.append(pts[0])
        elif op in ('closePath', 'endPath'):
            if cur:
                contours.append(cur)
            cur = []
        else:
            sys.exit('glyph %r: %s segments - not a rectilinear pixel font (use another --ttf or a rasteriser)' % (ch, op))
    if cur:
        contours.append(cur)
    return contours, font['hmtx'][name][0]


def fill(contours, unit, rows, asc_units):
    """Non-zero winding fill sampled at pixel centres. unit = font units per pixel."""
    cols = 0
    for c in contours:
        for x, _ in c:
            cols = max(cols, int(np.ceil(x / unit)) + 1)
    cols = max(cols, 1)
    xs = (np.arange(cols) + 0.5) * unit
    ys = asc_units - (np.arange(rows) + 0.5) * unit
    X, Y = np.meshgrid(xs, ys)
    wind = np.zeros(X.shape, int)
    for c in contours:
        n = len(c)
        for i in range(n):
            (x0, y0), (x1, y1) = c[i], c[(i + 1) % n]
            if y0 == y1:
                continue
            up = y1 > y0
            lo, hi = (y0, y1) if up else (y1, y0)
            cross = (Y >= lo) & (Y < hi)
            xi = x0 + (Y - y0) * (x1 - x0) / (y1 - y0)
            hit = cross & (X < xi)
            wind += np.where(hit, 1 if up else -1, 0)
    return wind != 0


def supersample(fn, w, h, ss=8):
    """Coverage of an implicit shape (fn(x, y) -> bool array, pixel units) over a w x h cell."""
    o = (np.arange(ss) + 0.5) / ss
    cov = np.zeros((h, w))
    for dy in o:
        for dx in o:
            X, Y = np.meshgrid(np.arange(w) + dx, np.arange(h) + dy)
            cov += fn(X, Y)
    return cov / (ss * ss)


def over(dst, rgb, cov):
    """Paints colour rgb (0..1) with coverage cov over dst (h, w, 4 float RGBA, premultiplied-free)."""
    a = cov[..., None]
    dst[..., :3] = dst[..., :3] * (1 - a) + np.array(rgb)[None, None, :] * a
    dst[..., 3] = dst[..., 3] * (1 - cov) + cov
    return dst


def hexc(s):
    return [int(s[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]


def disc(cx, cy, r):
    return lambda X, Y: (X - cx) ** 2 + (Y - cy) ** 2 <= r * r


def bubble_icon(face_hex, edge_hex, glyph_bitmap=None, glyph_hex='3A2408', dots=False, tail=True):
    """A 32x32 round speech bubble (tail bottom-left) with a font glyph or three dots in it."""
    img = np.zeros((32, 32, 4))
    shape = lambda X, Y: (((X - 16) ** 2 + (Y - 14.5) ** 2 <= 12.5 ** 2) |
                          (tail & (Y >= 22) & (Y <= 29.5) & (X >= 7.5 + (Y - 22) * 0.25) & (X <= 14 - (Y - 22) * 0.8)))
    inner = lambda X, Y: (((X - 16) ** 2 + (Y - 14.5) ** 2 <= 10.5 ** 2) |
                          (tail & (Y >= 23) & (Y <= 27.5) & (X >= 9.5 + (Y - 23) * 0.3) & (X <= 12.5 - (Y - 23) * 0.8)))
    over(img, hexc(edge_hex), supersample(shape, 32, 32))
    over(img, hexc(face_hex), supersample(inner, 32, 32))
    if glyph_bitmap is not None:
        gh, gw = glyph_bitmap.shape
        x0, y0 = 16 - gw // 2, int(round(14.5 - gh / 2))
        cov = np.zeros((32, 32))
        cov[y0:y0 + gh, x0:x0 + gw] = glyph_bitmap
        over(img, hexc(glyph_hex), cov)
    if dots:
        for cx in (10.5, 16, 21.5):
            over(img, hexc(glyph_hex), supersample(disc(cx, 15, 2.1), 32, 32))
    return img


def sweat_icon():
    img = np.zeros((32, 32, 4))
    # a drop: circle at the bottom, a point at the top (the tangent lines of a cone)
    def drop(X, Y, r=8.0, cx=16, cy=19, tip=3.0):
        circ = (X - cx) ** 2 + (Y - cy) ** 2 <= r * r
        k = (cy - tip)
        cone = (Y >= tip) & (Y <= cy) & (np.abs(X - cx) <= (Y - tip) / k * r * 1.06)
        return circ | cone
    over(img, hexc('1E4E8C'), supersample(lambda X, Y: drop(X, Y, 9.5, 16, 19, 1.5), 32, 32))
    over(img, hexc('8FD3FF'), supersample(lambda X, Y: drop(X, Y, 7.8, 16, 19.3, 4.2), 32, 32))
    over(img, hexc('F0FAFF'), supersample(disc(12.8, 20.5, 2.2), 32, 32))
    return img


def anger_icon():
    img = np.zeros((32, 32, 4))
    # the manga "popping vein": four arcs bulging outwards, one per quadrant, with gaps on the axes
    def veins(X, Y, w):
        out = np.zeros(X.shape, bool)
        for sx in (-1, 1):
            for sy in (-1, 1):
                cx, cy = 16 + sx * 12.5, 16 + sy * 12.5
                d = np.sqrt((X - cx) ** 2 + (Y - cy) ** 2)
                q = ((X - 16) * sx > 1.6) & ((Y - 16) * sy > 1.6)
                out |= q & (np.abs(d - 9.5) <= w)
        return out
    over(img, hexc('5A0C0C'), supersample(lambda X, Y: veins(X, Y, 2.6), 32, 32))
    over(img, hexc('FF3A30'), supersample(lambda X, Y: veins(X, Y, 1.35), 32, 32))
    return img


def triangle_icon(w, h, pts, fill_hex, edge_hex):
    """A filled triangle (pixel-space corners) with a 1 px darker edge."""
    img = np.zeros((h, w, 4))
    def tri(X, Y, grow):
        (ax, ay), (bx, by), (cx, cy) = pts
        def side(px, py, qx, qy):
            nx, ny = qy - py, px - qx
            ln = np.hypot(nx, ny)
            return ((X - px) * nx + (Y - py) * ny) / ln
        s1, s2, s3 = side(ax, ay, bx, by), side(bx, by, cx, cy), side(cx, cy, ax, ay)
        sgn = 1 if (bx - ax) * (cy - ay) - (by - ay) * (cx - ax) < 0 else -1
        return (sgn * s1 >= -grow) & (sgn * s2 >= -grow) & (sgn * s3 >= -grow)
    over(img, hexc(edge_hex), supersample(lambda X, Y: tri(X, Y, 0.0), w, h))
    over(img, hexc(fill_hex), supersample(lambda X, Y: tri(X, Y, -1.0), w, h))
    return img


def lock_icon():
    img = np.zeros((18, 16, 4))
    shackle = lambda X, Y, r0, r1: (((np.hypot(X - 8, Y - 7.5) <= r1) & (np.hypot(X - 8, Y - 7.5) >= r0) & (Y <= 7.5)) |
                                    ((Y > 7.5) & (Y <= 10) & (((X >= 8 - r1) & (X <= 8 - r0)) | ((X >= 8 + r0) & (X <= 8 + r1)))))
    body = lambda X, Y, g: (X >= 1.5 - g) & (X <= 14.5 + g) & (Y >= 8.5 - g) & (Y <= 17 + g)
    over(img, hexc('2A1A06'), supersample(lambda X, Y: shackle(X, Y, 2.4, 6.2), 16, 18))
    over(img, hexc('C8C8D0'), supersample(lambda X, Y: shackle(X, Y, 3.3, 5.3), 16, 18))
    over(img, hexc('2A1A06'), supersample(lambda X, Y: body(X, Y, 0.0), 16, 18))
    over(img, hexc('E8B848'), supersample(lambda X, Y: body(X, Y, -1.2), 16, 18))
    over(img, hexc('5A3C0A'), supersample(lambda X, Y: (np.hypot(X - 8, Y - 12) <= 1.6) | ((np.abs(X - 8) <= 0.7) & (Y >= 12) & (Y <= 15)), 16, 18))
    return img


def token_icon():
    """Wren's token (the field HUD's inventory): a gold medallion with a rim and a four-point star."""
    img = np.zeros((16, 16, 4))
    over(img, hexc('4A2E06'), supersample(disc(8, 8, 7.6), 16, 16))
    over(img, hexc('E8B848'), supersample(disc(8, 8, 6.4), 16, 16))
    over(img, hexc('A87A20'), supersample(lambda X, Y: np.abs(np.hypot(X - 8, Y - 8) - 4.6) <= 0.6, 16, 16))
    star = lambda X, Y: (np.abs(X - 8) + np.abs(Y - 8) * 2.4 <= 3.2) | (np.abs(X - 8) * 2.4 + np.abs(Y - 8) <= 3.2)
    over(img, hexc('FFF4C8'), supersample(star, 16, 16))
    return img


def argb4444(img):
    """float RGBA (0..1) -> ARGB4444 words (4-bit rounding)."""
    q = np.clip(np.floor(img * 15 + 0.5), 0, 15).astype(np.uint16)
    return (q[..., 3] << 12) | (q[..., 0] << 8) | (q[..., 1] << 4) | q[..., 2]


def build(ttf, scale, spacing):
    from fontTools.ttLib import TTFont
    font = TTFont(ttf)
    upem = font['head'].unitsPerEm
    px_em = 10 * scale
    unit = upem / px_em                                    # font units per output pixel
    asc = font['hhea'].ascent
    # the ink of every ASCII glyph lies between the baseline and the capital height (checked below)
    cap = max(max(y for c in (glyph_contours(font, chr(k))[0] or [[(0, 0)]]) for _, y in c) for k in range(33, 127))
    ink_h = int(round(cap / unit))
    rows = int(round(asc / unit)) + 4
    glyphs, cells = {}, {}
    for k in range(FIRST, FIRST + NUM):
        ch = chr(k)
        if k == 127:
            glyphs[k] = dict(bitmap=None, adv=0, x_off=0)
            continue
        contours, adv_u = glyph_contours(font, ch)
        if contours is None:
            sys.exit('the font has no glyph for %r' % ch)
        for c in contours:
            for x, y in c:
                if abs(x / unit - round(x / unit)) > 1e-6 or abs(y / unit - round(y / unit)) > 1e-6:
                    sys.exit('glyph %r: point (%s, %s) is off the %g-unit pixel grid at scale %d' % (ch, x, y, unit, scale))
                if y < 0 or y > cap:
                    sys.exit('glyph %r: ink outside the baseline..capital band (%s)' % (ch, y))
        bm = fill(contours, unit, rows, asc)
        ys, xs = np.nonzero(bm)
        if len(xs) == 0:
            glyphs[k] = dict(bitmap=None, adv=int(round(adv_u / unit)), x_off=0)
            continue
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        ink = bm[y0:y1, x0:x1]
        top = y0 - int(round((asc - cap) / unit))         # from the capital line
        glyphs[k] = dict(bitmap=ink, bearing=int(x0), top=int(top), adv=int(round(adv_u / unit)))
    dp = scale                                               # one design pixel (100 units) in output px
    space_adv = 4 * dp if spacing == 'ink' else glyphs[32]['adv']
    for k, g in glyphs.items():
        if g.get('bitmap') is None:
            g.update(adv=space_adv if k == 32 else g['adv'], x_off=0)
            continue
        if spacing == 'ink':
            g['x_off'] = 0
            g['adv'] = g['bitmap'].shape[1] + dp
        else:
            g['x_off'] = g['bearing']
    # pack unique shapes into the top half (rows of ink_h + 1 gutter)
    tex = np.zeros((TEX_H, TEX_W, 4))
    x = y = 1
    for k in sorted(glyphs):
        g = glyphs[k]
        if g.get('bitmap') is None:
            g.update(u=0, v=0, w=0, h=0, y_off=0)
            continue
        key = g['bitmap'].tobytes() + bytes(g['bitmap'].shape)
        if key not in cells:
            h, w = g['bitmap'].shape
            if x + w + 1 > TEX_W:
                x, y = 1, y + ink_h + 1
            if y + ink_h > 64:
                sys.exit('the glyphs do not fit the atlas top half (256x64) at scale %d' % scale)
            tex[y:y + h, x:x + w, :3] = 1.0
            tex[y:y + h, x:x + w, 3] = g['bitmap']
            cells[key] = (x, y, w, h)
            x += w + 1
        u, v, w, h = cells[key]
        g.update(u=u, v=v, w=w, h=h, y_off=g['top'])
    # icons (bottom half)
    gl = {c: glyphs[ord(c)]['bitmap'].astype(float) for c in '!?'}
    icons = {
        'marker_quest': bubble_icon('E8B848', '4A2E06', gl['!'], '3A2408'),
        'marker_talk': bubble_icon('B8BCC8', '30343E', None, '30343E', dots=True),
        'emote_excl': bubble_icon('F4F4F4', '30343E', gl['!'], 'D02820'),
        'emote_question': bubble_icon('F4F4F4', '30343E', gl['?'], '2850B0'),
        'emote_dots': bubble_icon('F4F4F4', '30343E', None, '30343E', dots=True),
        'emote_sweat': sweat_icon(),
        'emote_anger': anger_icon(),
        'advance': triangle_icon(12, 8, [(0.5, 0.5), (11.5, 0.5), (6, 7.5)], 'E8B848', '5A3C0A'),
        'cursor': triangle_icon(10, 12, [(0.5, 0.5), (9.5, 6), (0.5, 11.5)], 'E8B848', '5A3C0A'),
        'lock': lock_icon(),
        'token': token_icon(),
    }
    icon_rects = []
    slots = [(i * 32, 64) for i in range(7)] + [(224, 64), (240, 64), (224, 80), (240, 80)]
    for name, (ix, iy) in zip(ICONS, slots):
        im = icons[name]
        h, w = im.shape[:2]
        tex[iy:iy + h, ix:ix + w] = im
        icon_rects.append((ix, iy, w, h))
    while len(icon_rects) < 16:
        icon_rects.append((0, 0, 0, 0))
    texels = argb4444(tex)
    return dict(glyphs=glyphs, icons=icon_rects, texels=texels, ink_h=ink_h, space=space_adv, unit=unit,
                unique=len(cells), rows_used=y + ink_h, scale=scale, spacing=spacing)


def write(f, out_dir, report, preview):
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'dialog.tex'), 'wb') as fp:
        fp.write(struct.pack('<4HI20s', TEX_W, TEX_H, 2, 0, TEX_W * TEX_H * 2, b'\0' * 20))
        fp.write(f['texels'].astype('<u2').tobytes())
    b = bytearray(b'FNT1')
    b += struct.pack('<8BHH', 1, LINE_PITCH, f['ink_h'], FIRST, NUM, 16, f['space'], SHADOW, TEX_W, TEX_H)
    for k in range(FIRST, FIRST + NUM):
        g = f['glyphs'][k]
        b += struct.pack('<5Bbb B', g['u'], g['v'], g['w'], g['h'], g['adv'], g['x_off'], g['y_off'], 0)
    for r in f['icons']:
        b += struct.pack('<4B', *r)
    open(os.path.join(out_dir, 'dialog.fnt'), 'wb').write(bytes(b))
    if report:
        os.makedirs(os.path.dirname(report), exist_ok=True)
        rep = dict(source='assets/fonts/HomeVideo-Regular.ttf (CC0)', scale=f['scale'], spacing=f['spacing'],
                   line_pitch=LINE_PITCH, ink_height=f['ink_h'], space_advance=f['space'], shadow=SHADOW,
                   unique_glyph_cells=f['unique'], atlas_rows_used_of_64=int(f['rows_used']),
                   glyphs={chr(k): dict(adv=g['adv'], w=g['w'], h=g['h'], x_off=g['x_off'], y_off=g['y_off'])
                           for k, g in f['glyphs'].items() if k < 127},
                   icons={n: list(r) for n, r in zip(ICONS, f['icons'])})
        json.dump(rep, open(report, 'w'), indent=1, sort_keys=True)
    if preview:
        write_preview(f, preview)


def texels_rgba(t):
    return (np.stack([(t >> 8) & 15, (t >> 4) & 15, t & 15, (t >> 12) & 15], -1) * 17).astype(np.uint8)


def write_preview(f, path):
    from PIL import Image
    atlas = texels_rgba(f['texels'])
    W = 640
    im = Image.new('RGB', (W, 300 + 2 * TEX_H), (18, 16, 30))
    chk = np.indices((TEX_H, TEX_W)).sum(0) // 8 % 2
    bg = np.where(chk[..., None] == 1, np.array([60, 60, 70]), np.array([40, 40, 50]))
    a = atlas[..., 3:4] / 255.0
    comp = (atlas[..., :3] * a + bg * (1 - a)).astype(np.uint8)
    im.paste(Image.fromarray(comp).resize((TEX_W * 2, TEX_H * 2), Image.NEAREST), (10, 10))
    # sample text on the dialog box colour, 1:1, drawn exactly as src/text.cpp does (shadow + text)
    y0 = 2 * TEX_H + 24
    box = Image.new('RGB', (W - 20, 270), (28, 26, 44))
    canvas = np.asarray(box).astype(float)
    lines = ['OH! A KNIGHT. A REAL ONE. THANK THE STARS.', 'The dead are walking again.',
             'Two patrols came over the hill last night,', "Wren's blessing! Max HP +10, fully healed.",
             '0123456789 ()[]<>+-*/=%&#@$ ?!.,:;\'"_~^`', 'I will deal with them.   Tell me more.   Not now.']
    for li, text in enumerate(lines):
        for sh, col in ((SHADOW, (0, 0, 0)), (0, (244, 244, 244) if li != 3 else (255, 205, 117))):
            x, y = 12 + sh, 12 + li * LINE_PITCH * 2 + sh
            for ch in text:
                g = f['glyphs'].get(ord(ch))
                if g is None:
                    continue
                if g['w']:
                    cell = atlas[g['v']:g['v'] + g['h'], g['u']:g['u'] + g['w']]
                    al = cell[..., 3:4] / 255.0 * (0.75 if sh else 1.0)
                    gx, gy = x + g['x_off'], y + g['y_off']
                    canvas[gy:gy + g['h'], gx:gx + g['w']] = canvas[gy:gy + g['h'], gx:gx + g['w']] * (1 - al) + np.array(col) * al
                x += g['adv']
    im.paste(Image.fromarray(canvas.astype(np.uint8)), (10, y0))
    im.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ttf', default=os.path.join(ROOT, 'assets', 'fonts', 'HomeVideo-Regular.ttf'))
    ap.add_argument('--scale', type=int, default=2, help='output px per design pixel (the font has 10 per em)')
    ap.add_argument('--spacing', choices=('ink', 'mono'), default='ink')
    ap.add_argument('--out', default=os.path.join(ROOT, 'romdisk', 'fonts'))
    ap.add_argument('--report', default=os.path.join(ROOT, 'tools', 'dialog', 'reports', 'dialog_font.json'))
    ap.add_argument('--preview', default=os.path.join(ROOT, 'docs', 'dialog', 'font_preview.png'))
    a = ap.parse_args()
    f = build(a.ttf, a.scale, a.spacing)
    write(f, a.out, a.report, a.preview)
    print('wrote %s/dialog.tex (256x128 ARGB4444) + dialog.fnt: %d glyph shapes, capitals %d px, space %d px, '
          'line pitch %d px, glyph rows %d of 64 px' % (os.path.relpath(a.out, ROOT), f['unique'], f['ink_h'], f['space'],
                                                     LINE_PITCH, f['rows_used']))


if __name__ == '__main__':
    main()
