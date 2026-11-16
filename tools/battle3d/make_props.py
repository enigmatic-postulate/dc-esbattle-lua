"""make_props.py - procedural stand-ins for the barrow's graveyard set (GDD 16.2 asset A1: tombstones
and a crypt entrance are not in the uploaded kits). Low-poly static .dmf models textured with the
village kit's stone and brick textures (shared: no extra VRAM). Replace them with real art by
dropping new .dmf files of the same names into romdisk/models/. Host tool, deterministic.

    python3 tools/battle3d/make_props.py        -> romdisk/models/tomb_round.dmf, tomb_cross.dmf, crypt.dmf
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmfio  # noqa: E402
import strips as stripper  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
STONE, BRICK, ROOF, DARK = '/rd/tex/rocktrim.png', '/rd/tex/ubrick.png', '/rd/tex/roof.png', ''


class Mesh:
    def __init__(self):
        self.v = []          # (p, n, u, v, argb)
        self.tris = {}       # material -> [(a, b, c)]

    def quad(self, mat, p, uvs, argb=0xFFFFFFFF):
        """p: 4 corners counter-clockwise seen from the front."""
        p = [np.asarray(x, float) for x in p]
        n = np.cross(p[1] - p[0], p[2] - p[0]); n /= np.linalg.norm(n)
        base = len(self.v)
        for k in range(4):
            self.v.append((p[k], n, uvs[k][0], uvs[k][1], argb))
        self.tris.setdefault(mat, []).extend([(base, base + 1, base + 2), (base, base + 2, base + 3)])

    def poly(self, mat, p, uvs, argb=0xFFFFFFFF):
        """convex polygon (fan), counter-clockwise seen from the front."""
        p = [np.asarray(x, float) for x in p]
        n = np.cross(p[1] - p[0], p[2] - p[0]); n /= np.linalg.norm(n)
        base = len(self.v)
        for k in range(len(p)):
            self.v.append((p[k], n, uvs[k][0], uvs[k][1], argb))
        for k in range(1, len(p) - 1):
            self.tris.setdefault(mat, []).append((base, base + k, base + k + 1))

    def box(self, mat, lo, hi, uv_scale=0.5, skip_bottom=True):
        x0, y0, z0 = lo; x1, y1, z1 = hi
        s = uv_scale
        faces = [  # front (+z), back, left, right, top
            ([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (x1 - x0, y1 - y0)),
            ([(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (x1 - x0, y1 - y0)),
            ([(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (z1 - z0, y1 - y0)),
            ([(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (z1 - z0, y1 - y0)),
            ([(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (x1 - x0, z1 - z0)),
        ]
        for p, (w, h) in faces:
            self.quad(mat, p, [(0, h * s), (w * s, h * s), (w * s, 0), (0, 0)])

    def write(self, path):
        mats = sorted(self.tris, key=lambda m: (m == DARK, m))
        vt = np.zeros(len(self.v), dmfio.VTX)
        for i, (p, n, u, v, argb) in enumerate(self.v):
            n8 = np.clip(np.round(n * 127), -127, 127).astype(int)
            vt[i] = (p[0], p[1], p[2], u, v, n8[0], n8[1], n8[2], 255, argb, b'\0' * 4)
        groups = np.zeros(1, dmfio.GRP); groups[0] = (0, len(vt), dmfio.NO_BONE, dmfio.NO_BONE, 0)
        words, batches, materials = [], [], []
        for mi, mname in enumerate(mats):
            materials.append(dict(tex=mname, flags=dmfio.MAT_BILINEAR if mname else 0))
            st = stripper.stripify(self.tris[mname])
            assert stripper.check(self.tris[mname], st)
            first = len(words)
            for s in st:
                words.append(len(s)); words.extend(s)
            batches.append(dict(first_word=first, num_words=len(words) - first, num_strips=len(st), flags=mi << 8,
                                num_tris=len(self.tris[mname])))
        data = dmfio.write_dmf(vt, groups, batches, np.array(words, '<u2'), [], [], materials, None)
        open(path, 'wb').write(data)
        print('%s: %d tris, %d verts' % (os.path.basename(path), sum(b['num_tris'] for b in batches), len(vt)))


def tomb_round():
    m = Mesh()
    w, h, d, seg = 0.36, 0.75, 0.09, 8            # half width, shoulder height, half depth
    # front / back outlines: a rectangle with a half-disc on top
    outline = [(-w, 0.0), (w, 0.0)] + [(w * np.cos(np.pi * k / seg), h + w * np.sin(np.pi * k / seg)) for k in range(seg + 1)]
    s = 0.8
    for z, sign in ((d, 1), (-d, -1)):
        pts = [(x if sign > 0 else -x, y, z) for (x, y) in outline]
        m.poly(STONE, pts, [((px + w) * s, (h + w - py) * s) for (px, py, _) in pts])
    # sides and the rounded top as a ring of quads
    ring = [(w, 0.0)] + [(w * np.cos(np.pi * k / seg), h + w * np.sin(np.pi * k / seg)) for k in range(seg + 1)] + [(-w, 0.0)]
    for k in range(len(ring) - 1):
        (ax, ay), (bx, by) = ring[k], ring[k + 1]
        m.quad(STONE, [(ax, ay, d), (ax, ay, -d), (bx, by, -d), (bx, by, d)], [(0, 0), (0.15, 0), (0.15, 0.15), (0, 0.15)])
    # a slightly wider plinth
    m.box(STONE, (-w - 0.07, 0.0, -d - 0.07), (w + 0.07, 0.12, d + 0.07))
    return m


def tomb_cross():
    m = Mesh()
    m.box(STONE, (-0.08, 0.0, -0.08), (0.08, 1.05, 0.08), 0.8)
    m.box(STONE, (-0.34, 0.62, -0.075), (0.34, 0.78, 0.075), 0.8)
    m.box(STONE, (-0.22, 0.0, -0.2), (0.22, 0.14, 0.2), 0.8)
    return m


def crypt():
    m = Mesh()
    W, D, H = 1.6, 1.3, 2.3                       # half width, half depth, wall height
    m.box(BRICK, (-W, 0.0, -D), (W, H, D), 0.35)
    # pitched roof (front and back slopes, gables)
    R, O = 1.1, 0.25                              # ridge rise, overhang
    m.quad(ROOF, [(-W - O, H, D + O), (W + O, H, D + O), (W + O, H + R, 0.0), (-W - O, H + R, 0.0)], [(0, 1.2), (2.4, 1.2), (2.4, 0), (0, 0)])
    m.quad(ROOF, [(W + O, H, -D - O), (-W - O, H, -D - O), (-W - O, H + R, 0.0), (W + O, H + R, 0.0)], [(0, 1.2), (2.4, 1.2), (2.4, 0), (0, 0)])
    m.poly(BRICK, [(W, H, D), (W, H, -D), (W, H + R * 0.9, 0.0)], [(0, 0.6), (0.9, 0.6), (0.45, 0)])
    m.poly(BRICK, [(-W, H, -D), (-W, H, D), (-W, H + R * 0.9, 0.0)], [(0, 0.6), (0.9, 0.6), (0.45, 0)])
    # stone door frame and a dark doorway on the front (+z)
    m.box(STONE, (-0.85, 0.0, D), (-0.65, 1.75, D + 0.12), 0.8)
    m.box(STONE, (0.65, 0.0, D), (0.85, 1.75, D + 0.12), 0.8)
    m.box(STONE, (-0.85, 1.75, D), (0.85, 1.95, D + 0.12), 0.8)
    m.quad(DARK, [(-0.65, 0.0, D + 0.02), (0.65, 0.0, D + 0.02), (0.65, 1.75, D + 0.02), (-0.65, 1.75, D + 0.02)],
           [(0, 0)] * 4, 0xFF141018)
    # steps
    m.box(STONE, (-1.0, 0.0, D + 0.12), (1.0, 0.12, D + 0.5), 0.8)
    return m


def main():
    out = os.path.join(ROOT, 'romdisk', 'models')
    tomb_round().write(os.path.join(out, 'tomb_round.dmf'))
    tomb_cross().write(os.path.join(out, 'tomb_cross.dmf'))
    crypt().write(os.path.join(out, 'crypt.dmf'))


if __name__ == '__main__':
    main()
