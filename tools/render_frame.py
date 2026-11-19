"""render_frame.py - renders a host frame dump (battle_trace --dump DIR --dump-frames ...) to a PNG
with the PVR-like rasterizer in tools/battle3d/raster.c. Host tool; approximate (no PVR quirks
beyond z = 1/w depth, modulated textures, bilinear, and translucent/additive blending).

    python3 tools/render_frame.py /tmp/frames/frame_00120.txt romdisk out.png [--scale 1]

Order, as the KOS backend submits: OP list (3D batches, opaque sprites, OP rects), then TR list
(3D batches, alpha sprites, TR rects). OP pixels depth-test GREATER-OR-EQUAL and write depth; TR
primitives are sorted back to front (the PVR sorts per pixel) and only test depth.
Dump line formats (platform/host/host_engine.cpp):
  frame N / clear RRGGBB / tex I file PATH ALPHA / tex I mem W H FMT FILE / mat I TEX FLAGS
  b MAT COUNT + COUNT lines 'v|e x y z u v ARGB'   (3D batch; e = end of strip)
  s SPRITE cx cy w h angle filter u0 v0 u1 v1 ARGB z   (z = 0: automatic order depth)
  r OP|TR x y w h ARGB z
  fog RRGGBB start end   (slice 3D-6: PVR table fog on every 3D batch of the frame; MAT_MULTIPLY batches excepted)
"""
import argparse
import os
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'battle3d'))
import raster  # noqa: E402

FOG = None          # slice 3D-6: (rgb, start, end) of the frame being rendered

MAT_TRANSLUCENT, MAT_BILINEAR, MAT_CULL, MAT_ADDITIVE, MAT_MULTIPLY, MAT_CULL_FRONT = 1, 2, 4, 8, 16, 32


def load_texture(kind, args, dump_dir, romdisk):
    if kind == 'file':
        path, alpha = args[0], int(args[1])
        p = os.path.join(romdisk, path[4:]) if path.startswith('/rd/') else path
        im = np.array(Image.open(p).convert('RGBA'))
        if alpha:
            im[..., 3] = np.where(im[..., 3] >= 128, 255, 0)          # ARGB1555 (PNG_MASK_ALPHA)
        else:
            im[..., 3] = 255
        return im, bool(alpha)
    w, h, fmt, fname = int(args[0]), int(args[1]), int(args[2]), args[3]
    t = np.fromfile(os.path.join(dump_dir, fname), '<u2').reshape(h, w).astype(np.uint32)
    if fmt == 1:
        r, g, b = (t >> 11) & 31, (t >> 5) & 63, t & 31
        im = np.stack([r * 255 // 31, g * 255 // 63, b * 255 // 31, np.full_like(t, 255)], -1)
    elif fmt == 2:
        im = np.stack([(t >> 8) & 15, (t >> 4) & 15, t & 15, (t >> 12) & 15], -1) * 17
    else:
        im = np.stack([((t >> 10) & 31) * 255 // 31, ((t >> 5) & 31) * 255 // 31, (t & 31) * 255 // 31, ((t >> 15) & 1) * 255], -1)
    return im.astype(np.uint8), fmt != 1


def parse(path):
    tex, mats, batches, sprites, rects = {}, {}, [], [], []
    clear = 0
    global FOG
    FOG = None
    lines = open(path).read().split('\n')
    i = 0
    seq = 0
    while i < len(lines):
        ln = lines[i]
        p = ln.split()
        i += 1
        if not p:
            continue
        if p[0] == 'clear':
            clear = int(p[1], 16)
        elif p[0] == 'fog':
            FOG = (int(p[1], 16), float(p[2]), float(p[3]))
        elif p[0] == 'tex':
            tex[int(p[1])] = (p[2], p[3:])
        elif p[0] == 'mat':
            mats[int(p[1])] = (int(p[2]), int(p[3]))
        elif p[0] == 'b':
            m, n = int(p[1]), int(p[2])
            vs = []
            for k in range(n):
                q = lines[i + k].split()
                vs.append((q[0] == 'e', float(q[1]), float(q[2]), float(q[3]), float(q[4]), float(q[5]), int(q[6], 16)))
            i += n
            batches.append((m, vs))
        elif p[0] == 's':
            # s SPRITE cx cy w h angle filter u0 v0 u1 v1 ARGB z
            z = float(p[13])
            if z <= 0:
                z = 1.0 + seq * 1e-4          # automatic depth: later draws in front (engine.h)
            seq += 1
            sprites.append((int(p[1]), [float(x) for x in p[2:7]], int(p[7]), [float(x) for x in p[8:12]], int(p[12], 16), z))
        elif p[0] == 'r':
            rects.append((p[1], float(p[2]), float(p[3]), float(p[4]), float(p[5]), int(p[6], 16), float(p[7])))
    return clear, tex, mats, batches, sprites, rects


def strip_tris(vs):
    """Batch vertices (with end-of-strip flags) -> list of triangles (3 RV tuples), winding kept."""
    out, cur = [], []
    for v in vs:
        cur.append(v)
        if v[0]:
            for j in range(len(cur) - 2):
                a, b, c = cur[j], cur[j + 1], cur[j + 2]
                out.append((b, a, c) if j & 1 else (a, b, c))
            cur = []
    return out


def render(path, romdisk, scale=1):
    dump_dir = os.path.dirname(os.path.abspath(path))
    clear, tex, mats, batches, sprites, rects = parse(path)
    textures = {k: load_texture(kind, args, dump_dir, romdisk) for k, (kind, args) in tex.items()}
    cv = raster.Canvas(640, 480, clear)
    tr = []   # (sort z, kind, payload)

    def rv_of(tris):
        a = np.zeros(len(tris) * 3, raster.RV)
        k = 0
        for t in tris:
            for v in t:
                a[k] = (v[1], v[2], v[3], v[4], v[5], v[6]); k += 1
        return a

    for m, vs in batches:
        tid, flags = mats[m]
        tris = strip_tris(vs)
        if not tris:
            continue
        t = textures[tid][0] if tid else None
        if flags & (MAT_TRANSLUCENT | MAT_ADDITIVE | MAT_MULTIPLY):
            mode = raster.MUL if flags & MAT_MULTIPLY else (raster.ADD if flags & MAT_ADDITIVE else raster.BLEND)
            for tri in tris:
                tr.append((min(v[3] for v in tri), 'tri', (tri, t, bool(flags & MAT_BILINEAR), mode, bool(flags & MAT_CULL))))
        else:
            if FOG:
                cv.fog(True, *FOG)
            cv.tris(rv_of(tris), t, bool(flags & MAT_BILINEAR), raster.OP, 2 if flags & MAT_CULL_FRONT else bool(flags & MAT_CULL))
            cv.fog(False)

    def sprite_quad(s):
        sid, (cx, cy, w, h, ang), filt, (u0, v0, u1, v1), argb, z = s
        c, sn = np.cos(ang), np.sin(ang)
        pts = []
        for lx, ly, u, v in ((-w / 2, -h / 2, u0, v0), (w / 2, -h / 2, u1, v0), (-w / 2, h / 2, u0, v1), (w / 2, h / 2, u1, v1)):
            pts.append((False, cx + lx * c - ly * sn, cy + lx * sn + ly * c, z, u, v, argb))
        return [(pts[0], pts[1], pts[2]), (pts[1], pts[3], pts[2])]

    for s in sprites:
        t, alpha = textures[s[0]]
        if alpha:
            tr.append((s[5], 'spr', s))
        else:
            cv.tris(rv_of(sprite_quad(s)), t, s[2] == 2, raster.OP, False)
    for r in rects:
        lst, x, y, w, h, argb, z = r
        if lst == 'OP':
            cv.quad(x, y, w, h, argb, z)
        else:
            tr.append((z, 'rect', r))
    tr.sort(key=lambda e: e[0])
    for z, kind, pl in tr:
        if kind == 'tri':
            tri, t, bil, mode, cull = pl
            if FOG and mode != raster.MUL:
                cv.fog(True, *FOG)
            cv.tris(rv_of([tri]), t, bil, mode, cull)
            cv.fog(False)
        elif kind == 'spr':
            t, _ = textures[pl[0]]
            cv.tris(rv_of(sprite_quad(pl)), t, pl[2] == 2, raster.BLEND, False)
        else:
            _, x, y, w, h, argb, zz = pl
            cv.quad(x, y, w, h, argb, zz, raster.BLEND)
    img = cv.image()
    if scale != 1:
        img = img.resize((640 * scale, 480 * scale), Image.NEAREST)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump'); ap.add_argument('romdisk'); ap.add_argument('out')
    ap.add_argument('--scale', type=int, default=1)
    a = ap.parse_args()
    render(a.dump, a.romdisk, a.scale).save(a.out)


if __name__ == '__main__':
    main()
