"""make_props.py - procedural stand-ins for the barrow's graveyard set (GDD 16.2 asset A1: tombstones
and a crypt entrance are not in the uploaded kits). Low-poly static .dmf models textured with the
village kit's stone and brick textures (shared: no extra VRAM). Replace them with real art by
dropping new .dmf files of the same names into romdisk/models/. Host tool, deterministic.

    python3 tools/battle3d/make_props.py        -> romdisk/models/tomb_round.dmf, tomb_cross.dmf, crypt.dmf,
                                                   and (slice 3D-6, the barrow map) waystone.dmf, dead_tree.dmf, iron_fence.dmf
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

    def quad_out(self, mat, p, uvs, centre, argb=0xFFFFFFFF):
        """quad() with the winding chosen so the face points away from `centre` (slice 3D-6 props)."""
        p = [np.asarray(x, float) for x in p]
        n = np.cross(p[1] - p[0], p[2] - p[0])
        if np.dot(n, (p[0] + p[2]) * 0.5 - np.asarray(centre, float)) < 0:
            p = [p[0], p[3], p[2], p[1]]; uvs = [uvs[0], uvs[3], uvs[2], uvs[1]]
        self.quad(mat, p, uvs, argb)

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


# ---- slice 3D-6: the barrow map (docs/NEXT_SLICE_BARROW.md 7.2)
def top_poly(m, mat, pts, y, argb=0xFFFFFFFF):
    """A horizontal convex polygon facing up (+y), whatever the order of pts."""
    P = [np.array([x, y, z], float) for (x, z) in pts]
    if np.cross(P[1] - P[0], P[2] - P[0])[1] < 0:
        P = P[::-1]
    m.poly(mat, [tuple(p) for p in P], [((p[0] + 1) * 0.5, (p[2] + 1) * 0.5) for p in P], argb)


def prism(m, mat, pts, y0, y1, uv_scale=0.8, top=True, argb=0xFFFFFFFF):
    """A vertical prism over the convex polygon pts (x, z), y0..y1, faces pointing outwards."""
    n = len(pts)
    cx, cz = float(np.mean([p[0] for p in pts])), float(np.mean([p[1] for p in pts]))
    for k in range(n):
        (ax, az), (bx, bz) = pts[k], pts[(k + 1) % n]
        w = float(np.hypot(bx - ax, bz - az)) * uv_scale
        h = (y1 - y0) * uv_scale
        m.quad_out(mat, [(ax, y0, az), (bx, y0, bz), (bx, y1, bz), (ax, y1, az)], [(0, h), (w, h), (w, 0), (0, 0)], (cx, (y0 + y1) * 0.5, cz), argb)
    if top:
        top_poly(m, mat, pts, y1, argb)


def waystone():
    """The barrow's checkpoint stone (GDD App. A barrow_stone, decision BA1): a leaning standing stone on a plinth, a
    rune band carved in a darker, faintly glowing stone."""
    m = Mesh()
    base = [(-0.42, -0.34), (0.42, -0.34), (0.48, 0.34), (-0.48, 0.34)]
    prism(m, STONE, base, 0.0, 0.16, top=True)
    # the shaft: an irregular octagon tapering upwards
    ring0 = [(0.26 * np.cos(a) * (1.0 + 0.12 * np.sin(3 * a)), 0.19 * np.sin(a)) for a in np.linspace(0, 2 * np.pi, 8, endpoint=False)]
    ring1 = [(x * 0.72 + 0.04, z * 0.78) for (x, z) in ring0]
    y0, y1 = 0.16, 1.55
    n = len(ring0)
    for k in range(n):
        a0, b0 = ring0[k], ring0[(k + 1) % n]
        a1, b1 = ring1[k], ring1[(k + 1) % n]
        m.quad_out(STONE, [(a0[0], y0, a0[1]), (b0[0], y0, b0[1]), (b1[0], y1, b1[1]), (a1[0], y1, a1[1])], [(0, 1.1), (0.3, 1.1), (0.3, 0), (0, 0)],
                   (0.02, (y0 + y1) * 0.5, 0.0))
    top_poly(m, STONE, ring1, y1)                                  # the cap
    # the rune band: a slightly proud, darker ring of quads half way up, on the front faces
    ry0, ry1 = 0.78, 0.98
    for k in range(n):
        a0, b0 = ring0[k], ring0[(k + 1) % n]
        f0 = (ry0 - y0) / (y1 - y0); f1 = (ry1 - y0) / (y1 - y0)
        lerp = lambda p, q, f: (p[0] + (q[0] - p[0]) * f, p[1] + (q[1] - p[1]) * f)
        p0, q0 = lerp(a0, ring1[k], f0), lerp(b0, ring1[(k + 1) % n], f0)
        p1, q1 = lerp(a0, ring1[k], f1), lerp(b0, ring1[(k + 1) % n], f1)
        s = 1.06
        m.quad_out(DARK, [(p0[0] * s, ry0, p0[1] * s), (q0[0] * s, ry0, q0[1] * s), (q1[0] * s, ry1, q1[1] * s), (p1[0] * s, ry1, p1[1] * s)],
                   [(0, 0)] * 4, (0.02, (ry0 + ry1) * 0.5, 0.0), 0xFF8A6CE0)
    return m


def dead_tree():
    """A withered tree (GDD App. A / the barrow's border): a bent trunk and five bare branches, dark bark (untextured,
    vertex colour), ~90 triangles - cheaper than the kit's leafy tree."""
    m = Mesh()
    BARK, BARK2 = 0xFF4A3A30, 0xFF3A2E28

    def limb(p0, p1, r0, r1, sides=5, col=BARK):
        p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
        d = p1 - p0; d /= np.linalg.norm(d)
        a = np.cross(d, [0.0, 0.0, 1.0] if abs(d[2]) < 0.9 else [1.0, 0.0, 0.0]); a /= np.linalg.norm(a)
        b = np.cross(d, a)
        ring = lambda c, r: [c + r * (np.cos(t) * a + np.sin(t) * b) for t in np.linspace(0, 2 * np.pi, sides, endpoint=False)]
        r0s, r1s = ring(p0, r0), ring(p1, r1)
        for k in range(sides):
            m.quad_out(DARK, [r0s[k], r0s[(k + 1) % sides], r1s[(k + 1) % sides], r1s[k]], [(0, 0)] * 4, (p0 + p1) * 0.5, col)
        return p1

    top = limb((0, 0, 0), (0.08, 1.3, 0.02), 0.16, 0.10, 6)
    top2 = limb(top, (-0.05, 2.25, 0.08), 0.10, 0.06, 5, BARK2)
    limb(top, (0.75, 2.0, 0.2), 0.07, 0.025, 4)
    limb(top, (-0.7, 1.75, -0.25), 0.065, 0.025, 4, BARK2)
    limb(top2, (0.5, 2.75, -0.3), 0.05, 0.02, 4)
    limb(top2, (-0.45, 2.9, 0.25), 0.05, 0.02, 4, BARK2)
    limb((0.05, 0.9, 0.0), (0.35, 1.35, 0.55), 0.05, 0.02, 4)
    return m


def iron_fence():
    """One segment of wrought-iron graveyard fence (the barrow's `#` runs, 1 tile, along x): posts, two rails,
    spiked bars. Untextured, dark metal."""
    m = Mesh()
    IRON, IRON2 = 0xFF2A2C34, 0xFF3C3E48
    for px in (-0.5, 0.5):                                         # the posts at the segment's ends (shared by neighbours)
        m.box(DARK, (px - 0.05, 0.0, -0.05), (px + 0.05, 1.08, 0.05))
        top_poly(m, DARK, [(px - 0.07, -0.07), (px + 0.07, -0.07), (px + 0.07, 0.07), (px - 0.07, 0.07)], 1.08, IRON2)
    for y in (0.18, 0.82):
        m.box(DARK, (-0.5, y, -0.02), (0.5, y + 0.05, 0.02))
    for k in range(4):
        x = -0.3 + 0.2 * k
        m.box(DARK, (x - 0.015, 0.1, -0.015), (x + 0.015, 0.95, 0.015))
        m.poly(DARK, [(x - 0.035, 0.95, 0.0), (x + 0.035, 0.95, 0.0), (x, 1.07, 0.0)], [(0, 0)] * 3, IRON2)
        m.poly(DARK, [(x + 0.035, 0.95, 0.0), (x - 0.035, 0.95, 0.0), (x, 1.07, 0.0)], [(0, 0)] * 3, IRON2)
    # the box faces were emitted white: darken every vertex to iron
    m.v = [(p, n, u, v, IRON if argb == 0xFFFFFFFF else argb) for (p, n, u, v, argb) in m.v]
    return m


def main():
    out = os.path.join(ROOT, 'romdisk', 'models')
    tomb_round().write(os.path.join(out, 'tomb_round.dmf'))
    tomb_cross().write(os.path.join(out, 'tomb_cross.dmf'))
    crypt().write(os.path.join(out, 'crypt.dmf'))
    waystone().write(os.path.join(out, 'waystone.dmf'))        # slice 3D-6
    dead_tree().write(os.path.join(out, 'dead_tree.dmf'))
    iron_fence().write(os.path.join(out, 'iron_fence.dmf'))


if __name__ == '__main__':
    main()
