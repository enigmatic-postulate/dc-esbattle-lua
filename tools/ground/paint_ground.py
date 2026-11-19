#!/usr/bin/env python3
"""paint_ground.py - the village ground, painted offline (docs/GROUND_ART.md).

The VS1 ground (generated at boot, 4 texels per tile, mint grass and sand) read as washed out and empty next to the
kit's textures. This paints one texture for the whole map at 12 texels per tile (about 5 screen pixels per texel at
the field camera, the density of the characters' and props' textures), in the village's own palette, with:

  * grass: the kit's terrain noise (T_Noise_Terrain, tileable, three channels) for large patches, fbm for smaller
    ones, three greens taken from the kit (the vines, the roof moss) and blade / tuft / clover detail;
  * dirt: the wood and boulder browns, noisy edges that wander off the tile grid, a worn band where grass thins
    out, wagon ruts along the main road, a trodden centre on the footpaths, pebbles in the kit's stone greys with
    a lit and a shaded side (the light of the baked props);
  * flowers in clusters on the map's `,` tiles, a few wild ones elsewhere;
  * the props' shadows (their real triangles, projected along VS1's bake light) and contact darkening at their
    feet, so houses, crates, the wagon and boulders sit on the ground;
  * a one-tile margin around the map that darkens into the outer ground.

Output: romdisk/maps/<name>.gnd, a 512x512 RGB565 texture, VQ-compressed (the PVR's native 2 bits per texel:
2 KiB codebook + 64 KiB of indices, as much VRAM as the VS1 ground's 256x128 RGB565), and optional previews.

    python3 tools/ground/paint_ground.py [romdisk/maps/village.txt] [--preview OUT.png] [--raw OUT.png] [--check]

Deterministic (fixed seeds, no threads): `--check` rebuilds in memory and compares with the file.
"""
import argparse, os, struct, sys
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'vs1art'))
import dmfio  # noqa: E402
import pvrtex  # noqa: E402

TPT, MARGIN, SIZE = 12, 1, 512
KIT_NOISE = os.path.join(ROOT, 'assets', 'ground', 'T_Noise_Terrain.png')
LIGHT = np.array([-0.38, 0.82, 0.43]); LIGHT /= np.linalg.norm(LIGHT)    # towards the light (VS1's bake)

# palette (sRGB 0..255): from the kit textures (romdisk/tex; docs/GROUND_ART.md has the samples)
G_DARK, G_MID, G_LIGHT, G_OLIVE = (52, 104, 36), (72, 140, 46), (104, 162, 56), (118, 146, 40)   # vines 44 99 33, moss 6B 89 0D
D_BASE, D_LIGHT, D_DARK, D_DAMP = (142, 102, 60), (172, 130, 80), (106, 74, 44), (86, 60, 36)    # wood 8B 69 4C, boulders 67 4F 39
STONES = [(150, 140, 126), (128, 120, 110), (166, 154, 136), (118, 100, 82)]                    # rocktrim, brick, boulders
FLOWERS = [(244, 240, 226), (250, 214, 92), (214, 120, 168), (138, 150, 232)]
OUTER = (30, 56, 28)
CLOVER = (44, 104, 44)

# Slice 3D-6: a palette per map (docs/NEXT_SLICE_BARROW.md 8). The barrow: dead olive grass, grey-brown earth, a pale
# stone-grey path, moss; painted at full brightness and night-tinted at runtime (light `night`, src/lights.h).
PALETTES = {
    'village': None,                                   # the constants above (byte-identical to slice 3D-5's ground)
    'barrow': dict(G_DARK=(62, 70, 40), G_MID=(84, 92, 52), G_LIGHT=(110, 114, 66), G_OLIVE=(104, 96, 56),
                   D_BASE=(120, 110, 96), D_LIGHT=(150, 142, 128), D_DARK=(86, 78, 68), D_DAMP=(70, 64, 58),
                   STONES=[(150, 146, 140), (126, 122, 118), (170, 166, 158), (110, 104, 98)],
                   FLOWERS=[(226, 222, 236), (196, 184, 226), (236, 232, 214)], OUTER=(22, 26, 20), CLOVER=(58, 84, 52)),
}


def use_palette(name):
    pal = PALETTES.get(name)
    if pal:
        globals().update(pal)


# ------------------------------------------------------------------ noise helpers (deterministic, vectorised)
def hash2(x, y, seed):
    h = (x.astype(np.uint64) * np.uint64(374761393) + y.astype(np.uint64) * np.uint64(668265263) + np.uint64(seed) * np.uint64(2246822519)) & np.uint64(0xFFFFFFFF)
    h = ((h ^ (h >> np.uint64(13))) * np.uint64(1274126177)) & np.uint64(0xFFFFFFFF)
    return ((h ^ (h >> np.uint64(16))) & np.uint64(0xFFFF)).astype(np.float64) / 65535.0


def value_noise(X, Z, seed):
    xi, zi = np.floor(X).astype(np.int64), np.floor(Z).astype(np.int64)
    fx, fz = X - xi, Z - zi
    sx, sz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    xi, zi = xi + 1000, zi + 1000
    a = hash2(xi, zi, seed) + (hash2(xi + 1, zi, seed) - hash2(xi, zi, seed)) * sx
    b = hash2(xi, zi + 1, seed) + (hash2(xi + 1, zi + 1, seed) - hash2(xi, zi + 1, seed)) * sx
    return a + (b - a) * sz


def fbm(X, Z, seed, octaves=4, lac=2.0, gain=0.5):
    s, amp, tot = 0.0, 1.0, 0.0
    for o in range(octaves):
        s = s + amp * value_noise(X * lac ** o, Z * lac ** o, seed + 17 * o)
        tot += amp; amp *= gain
    return s / tot


def blur(a, sigma):
    """separable gaussian (edges clamped)"""
    if sigma <= 0:
        return a
    r = int(3 * sigma + 0.5)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2); k /= k.sum()
    out = a
    for axis in (0, 1):
        p = [(0, 0)] * out.ndim; p[axis] = (r, r)
        padded = np.pad(out, p, mode='edge')
        acc = np.zeros_like(out, dtype=np.float64)
        for i, w in enumerate(k):
            sl = [slice(None)] * out.ndim; sl[axis] = slice(i, i + out.shape[axis])
            acc += w * padded[tuple(sl)]
        out = acc
    return out


def sample(a, fi, fj):
    """bilinear lookup of a (rows j, columns i) at fractional texel positions (edges clamped)"""
    H, W = a.shape
    fi = np.clip(fi - 0.5, 0, W - 1.001); fj = np.clip(fj - 0.5, 0, H - 1.001)
    i0, j0 = np.floor(fi).astype(int), np.floor(fj).astype(int)
    u, v = fi - i0, fj - j0
    top = a[j0, i0] * (1 - u) + a[j0, i0 + 1] * u
    bot = a[j0 + 1, i0] * (1 - u) + a[j0 + 1, i0 + 1] * u
    return top * (1 - v) + bot * v


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def mix(c0, c1, t):
    return np.asarray(c0, float) * (1 - t[..., None]) + np.asarray(c1, float) * t[..., None]


def kit_noise(X, Z, period, ch):
    """the kit's tileable terrain noise, channel ch, bilinear, one repeat per `period` tiles (0..1)"""
    img = np.asarray(Image.open(KIT_NOISE).convert('RGBA'), np.float64)[..., ch]
    img = (img - img.min()) / (img.max() - img.min())
    n = img.shape[0]
    u, v = (X / period % 1.0) * n - 0.5, (Z / period % 1.0) * n - 0.5
    i0, j0 = np.floor(u).astype(int), np.floor(v).astype(int)
    fu, fv = u - i0, v - j0
    g = lambda i, j: img[j % n, i % n]
    a = g(i0, j0) + (g(i0 + 1, j0) - g(i0, j0)) * fu
    b = g(i0, j0 + 1) + (g(i0 + 1, j0 + 1) - g(i0, j0 + 1)) * fu
    return a + (b - a) * fv


# ------------------------------------------------------------------ the map
def read_map(path):
    L = open(path).read().split('\n')
    name = next(l.split()[1] for l in L if l.startswith('name '))
    i = next(k for k, l in enumerate(L) if l.startswith('map '))
    w, h = map(int, L[i].split()[1:3])
    rows = L[i + 1:i + 1 + h]
    props = []
    for l in L[i + 1 + h:]:
        p = l.split()
        if p and p[0] == 'prop':
            solid = (int(p[6]), int(p[7])) if len(p) >= 8 and p[5] == 'solid' else None
            props.append((p[1], float(p[2]), float(p[3]), float(p[4]), solid))
    return name, w, h, rows, props


def prop_model(model, rot):
    for n in ('%s_r%d' % (model, int(rot) % 360), model):
        p = os.path.join(ROOT, 'romdisk', 'models', n + '.dmf')
        if os.path.exists(p):
            return dmfio.read_dmf(p)
    raise FileNotFoundError(model)


def prop_triangles(props):
    """world-space triangles of every prop (as the field places them: mat_trs_y(rot) + (x, 0, y))"""
    out = []
    for model, x, z, rot, _ in props:
        m = prop_model(model, rot)
        v = m['verts']
        yaw = np.radians(rot)
        c, s = np.cos(yaw), np.sin(yaw)
        P = np.stack([c * v['x'] + s * v['z'] + x, v['y'], -s * v['x'] + c * v['z'] + z], 1).astype(np.float64)
        tris = [t for k in range(len(m['batches'])) for st in dmfio.strips_of_batch(m, k) for t in dmfio.strip_triangles(st)]
        out.append(P[np.array(tris)])
    return np.concatenate(out) if out else np.zeros((0, 3, 3))


def raster_mask(polys, ss=2):
    """polygons in texel coordinates -> anti-aliased coverage (SIZE x SIZE), ss x ss supersampling"""
    im = Image.new('L', (SIZE * ss, SIZE * ss), 0)
    d = ImageDraw.Draw(im)
    for poly in polys:
        d.polygon([(float(px) * ss, float(py) * ss) for px, py in poly], fill=255)
    a = np.asarray(im, np.float64) / 255.0
    return a.reshape(SIZE, ss, SIZE, ss).mean((1, 3))


def to_tex(X, Z):
    return (X + MARGIN) * TPT, (Z + MARGIN) * TPT


# ------------------------------------------------------------------ painting
def paint(map_path):
    name, w, h, rows, props = read_map(map_path)
    use_palette(name)
    assert (w + 2 * MARGIN) * TPT <= SIZE and (h + 2 * MARGIN) * TPT <= SIZE, 'map too large for the ground texture'
    rng = np.random.default_rng(20261117)
    jj, ii = np.mgrid[0:SIZE, 0:SIZE].astype(np.float64)
    X, Z = (ii + 0.5) / TPT - MARGIN, (jj + 0.5) / TPT - MARGIN
    tx, tz = np.floor(X).astype(int), np.floor(Z).astype(int)
    inside = (tx >= 0) & (tz >= 0) & (tx < w) & (tz < h)
    ch = np.full((h, w), '.')
    for r in range(h):
        ch[r] = list(rows[r])
    tile = np.where(inside, ch[np.clip(tz, 0, h - 1), np.clip(tx, 0, w - 1)], '.')

    # --- the path field: tiles, blurred, pushed around by noise (edges off the tile grid)
    path_t = (tile == '=').astype(np.float64)
    fb = blur(path_t, 0.30 * TPT)
    # domain warp: the edge wanders up to ~0.25 tile off the grid, with a finer ragged wobble on top
    wi = TPT * (0.50 * (fbm(X * 0.8, Z * 0.8, 13, 3) - 0.5) + 0.12 * (value_noise(X * 4.0, Z * 4.0, 14) - 0.5))
    wj = TPT * (0.50 * (fbm(X * 0.8, Z * 0.8, 15, 3) - 0.5) + 0.12 * (value_noise(X * 4.0, Z * 4.0, 16) - 0.5))
    f = sample(fb, ii + wi, jj + wj)
    f = f + 0.10 * (value_noise(X * 6.0, Z * 6.0, 12) - 0.5)
    path = smoothstep(0.44, 0.56, f)                     # 1 = dirt
    worn = smoothstep(0.30, 0.48, f) * (1 - path)        # the thinning grass beside it

    # --- grass
    big = kit_noise(X, Z, 9.0, 0)
    mid = kit_noise(X + 3.1, Z + 1.7, 3.2, 1)
    fine = fbm(X * 1.3, Z * 1.3, 3, 4)
    g = mix(G_DARK, G_MID, smoothstep(0.15, 0.75, big))
    g = mix(g, G_LIGHT, smoothstep(0.55, 0.95, mid) * 0.55)
    g = mix(g, G_OLIVE, smoothstep(0.62, 0.9, fine) * 0.45)
    # blades: per-texel jitter stretched vertically (the camera looks down at 45 degrees: blades read as strokes)
    blade = hash2(ii.astype(np.int64), (jj // 2).astype(np.int64) + (ii.astype(np.int64) % 2) * 7, 5)
    g = g * (0.91 + 0.15 * blade[..., None])
    # tufts: short strokes, dark at the root, light at the tip
    tuft = np.zeros((SIZE, SIZE)); tip = np.zeros((SIZE, SIZE))
    n_tufts = int(w * h * 2.2)
    cx, cz = rng.uniform(-MARGIN, w + MARGIN, n_tufts), rng.uniform(-MARGIN, h + MARGIN, n_tufts)
    for x0, z0 in zip(cx, cz):
        bi, bj = int((x0 + MARGIN) * TPT), int((z0 + MARGIN) * TPT)
        for b in range(int(rng.integers(3, 7))):
            i = bi + int(rng.integers(-3, 4)); j = bj + int(rng.integers(-1, 2)); L = int(rng.integers(2, 5))
            lean = int(rng.integers(-1, 2))
            for k in range(L):
                ik, jk = i + (lean * k) // 2, j - k
                if 0 <= ik < SIZE and 0 <= jk < SIZE:
                    if k == 0:
                        tuft[jk, ik] = 1.0
                    else:
                        tip[jk, ik] = max(tip[jk, ik], k / (L - 1 + 1e-9))
    g = g * (1 - 0.30 * tuft[..., None])
    g = mix(g, np.array(G_LIGHT) * 1.12, 0.55 * tip)
    # clover patches: small round leaves in darker, bluer green
    clover = smoothstep(0.72, 0.8, fbm(X * 2.3, Z * 2.3, 21, 3)) * (hash2(ii.astype(np.int64) // 2, jj.astype(np.int64) // 2, 22) > 0.45)
    g = mix(g, CLOVER, 0.6 * clover)

    # --- dirt
    dn = kit_noise(X + 7.3, Z + 2.2, 4.0, 2)
    dn2 = fbm(X * 2.7, Z * 2.7, 31, 3)
    d = mix(D_DARK, D_BASE, smoothstep(0.1, 0.6, dn))
    d = mix(d, D_LIGHT, smoothstep(0.55, 0.95, dn2) * 0.6)
    d = d * (0.92 + 0.16 * hash2(ii.astype(np.int64), jj.astype(np.int64), 33)[..., None])
    # the main road (a row run of path tiles >= 20 long): wagon ruts; footpaths: a trodden, lighter centre
    road_rows = [r for r in range(h) if rows[r].count('=') >= 20]
    road = np.isin(tz, road_rows) & (tile == '=')
    if road_rows:
        zc = (min(road_rows) + max(road_rows) + 1) / 2.0
        rut = np.zeros_like(X)
        for k, off in enumerate((-0.42, 0.42)):
            wob = 0.10 * (fbm(X * 0.45, Z * 0.1 + 5 * k, 41 + k, 2) - 0.5)          # the tracks meander...
            keep = smoothstep(0.30, 0.45, fbm(X * 0.35, Z * 0.1, 43 + k, 2))       # ...and fade in and out
            dz = np.abs(Z - (zc + off) - wob)
            rut = np.maximum(rut, smoothstep(0.10, 0.03, dz) * keep)
        rutmask = blur(road.astype(float), 0.5 * TPT) > 0.5
        d = mix(d, D_DAMP, 0.55 * rut * rutmask)
        d = mix(d, D_LIGHT, 0.25 * smoothstep(0.30, 0.18, np.abs(Z - zc)) * rutmask * (1 - rut))
    foot = (tile == '=') & ~road
    if foot.any():
        # distance to the footpath's own axis: the centre of its run of path tiles across
        axis = np.zeros_like(X)
        for r in range(h):
            for c in range(w):
                if rows[r][c] != '=' or r in road_rows:
                    continue
                vertical = (r > 0 and rows[r - 1][c] == '=') or (r + 1 < h and rows[r + 1][c] == '=')
                if vertical:
                    c0 = c
                    while c0 > 0 and rows[r][c0 - 1] == '=': c0 -= 1
                    c1 = c
                    while c1 + 1 < w and rows[r][c1 + 1] == '=': c1 += 1
                    sel = (tz == r) & (tx == c)
                    axis[sel] = np.abs(X[sel] - (c0 + c1 + 1) / 2.0)
        trod = smoothstep(0.55, 0.15, axis) * foot
        d = mix(d, D_LIGHT, 0.35 * blur(trod, 2.0))

    col = mix(g, d, path)
    # the worn band: grass thinning out into soil, damp dark soil right at the edge
    col = mix(col, mix(g * 0.9, D_DAMP, 0.5 * np.ones_like(X)), 0.55 * worn * (hash2(ii.astype(np.int64), jj.astype(np.int64), 51) > 0.35))
    edge = smoothstep(0.42, 0.5, f) * smoothstep(0.62, 0.5, f)
    col = col * (1 - 0.16 * edge[..., None])

    # --- grit and pebbles on the dirt: mostly specks a shade off the soil, some stones in the kit's greys, round,
    # lit from the upper left with a shadow to the lower right (sub-texel shapes are anti-aliased)
    pts = np.argwhere(path > 0.8)
    n_grit = int(path.sum() / (TPT * TPT) * 14)
    if len(pts):
        for (pj, pi) in pts[rng.integers(0, len(pts), n_grit)]:
            col[pj, pi] = col[pj, pi] * rng.choice([0.78, 0.86, 1.12, 1.2])
        n_peb = int(path.sum() / (TPT * TPT) * 2.5)
        for (pj, pi) in pts[rng.integers(0, len(pts), n_peb)]:
            r = rng.uniform(1.0, 2.3)
            stone = np.array(STONES[int(rng.integers(0, len(STONES)))], float)
            base = 0.55 * stone + 0.45 * col[pj, pi]                  # dusty: half the soil's colour
            R = int(np.ceil(r)) + 2
            for dj in range(-R, R + 1):
                for di in range(-R, R + 1):
                    j, i = pj + dj, pi + di
                    if not (0 <= i < SIZE and 0 <= j < SIZE):
                        continue
                    q = np.sqrt((di / r) ** 2 + (dj / (r * 0.75)) ** 2)
                    cover = np.clip(1.5 - q, 0, 1) if q > 0.5 else 1.0
                    sq = np.sqrt(((di - 0.8) / r) ** 2 + ((dj - 0.8) / (r * 0.75)) ** 2)   # the shadow, offset
                    shadow = np.clip(1.4 - sq, 0, 1) * (1 - cover)
                    shade = 1.10 - 0.16 * (di + dj) / (2 * r)          # lit from the upper left
                    col[j, i] = col[j, i] * (1 - cover) * (1 - 0.22 * shadow) + base * shade * cover

    # --- flowers: clusters on ',' tiles, a few wild ones
    fl_tiles = np.argwhere(ch == ',')
    clusters = [(c + 0.5, r + 0.5, 5) for r, c in fl_tiles]
    wild = rng.uniform(0, 1, (int(w * h * 0.05), 2)) * (w, h)
    clusters += [(x, z, 2) for x, z in wild if ch[int(z), int(x)] == '.']
    for (fx, fz, n) in clusters:
        kind = FLOWERS[int(rng.integers(0, len(FLOWERS)))]
        for k in range(int(n) + int(rng.integers(0, 4))):
            x = fx + rng.normal(0, 0.28); z = fz + rng.normal(0, 0.28)
            i, j = int((x + MARGIN) * TPT), int((z + MARGIN) * TPT)
            if not (1 <= i < SIZE - 1 and 1 <= j < SIZE - 1) or path[j, i] > 0.3:
                continue
            col[j + 1, i] = col[j + 1, i] * 0.7                        # the stem's shade
            col[j, i] = kind
            if rng.uniform() < 0.5:
                col[j, i + 1] = np.asarray(kind) * 0.85

    # --- props: shadows along the bake light, contact darkening at their feet
    T = prop_triangles(props)
    if len(T):
        G = T - LIGHT[None, None, :] * (T[:, :, 1:2] / LIGHT[1])
        ti, tj = to_tex(G[:, :, 0], G[:, :, 2])
        shadow = raster_mask([list(zip(a, b)) for a, b in zip(ti, tj)])
        low = T[:, :, 1].max(1) < 0.35
        fi, fj = to_tex(T[low][:, :, 0], T[low][:, :, 2])
        feet = raster_mask([list(zip(a, b)) for a, b in zip(fi, fj)])
        # houses and crates stand on their whole footprint; the open props (wagon, fences, arch) and the boulders
        # only where their triangles meet the ground
        closed = [p for p in props if p[4] and (p[0].startswith('house') or p[0] == 'crate')]
        for _, x, z, _, solid in closed:
            fw, fh = solid
            i0, j0 = to_tex(x - fw / 2.0, z - fh / 2.0)
            i1, j1 = to_tex(x + fw / 2.0, z + fh / 2.0)
            feet[int(j0):int(j1), int(i0):int(i1)] = 1.0
        soft = 0.55 * blur(shadow, 1.2) + 0.45 * blur(shadow, 4.0)
        inside = np.zeros_like(X)                                     # under a house: seen only through its ghost
        for model, x, z, _, solid in closed:
            if model.startswith('house'):
                fw, fh = solid
                i0, j0 = to_tex(x - fw / 2.0, z - fh / 2.0)
                i1, j1 = to_tex(x + fw / 2.0, z + fh / 2.0)
                inside[int(j0):int(j1), int(i0):int(i1)] = 1.0
        ao = blur(np.clip(feet, 0, 1), 0.35 * TPT) * (1 - inside)     # contact darkening around the feet, not under
        dark = np.clip(0.40 * soft + 0.30 * ao, 0, 0.6) * (1 - inside) + 0.22 * inside
        tint = np.array([0.86, 0.92, 1.0])
        col = mix(col, D_DARK, 0.55 * blur(inside, 1.5))               # a packed-earth floor under the houses
        col = col * (1 - dark[..., None]) * (1 - (1 - tint) * dark[..., None] / 0.6)

    # --- outside the map: the margin darkens into the outer ground, the rest is flat
    dx = np.maximum(np.maximum(-X, X - w), 0); dz = np.maximum(np.maximum(-Z, Z - h), 0)
    out_d = np.maximum(dx, dz)
    col = mix(col, OUTER, smoothstep(0.0, MARGIN, out_d))
    region = (X < w + MARGIN) & (Z < h + MARGIN)
    col[~region] = OUTER
    return name, np.clip(col, 0, 255), (w, h)


# ------------------------------------------------------------------ VQ (the PVR's: 256 codes of 2x2 RGB565 texels)
def vq_encode(img, iters=24, seed=7):
    """img (SIZE, SIZE, 3) float 0..255 -> (codebook (256, 4) RGB565 in twiddled texel order, indices (SIZE/2, SIZE/2))"""
    n = SIZE // 2
    blk = img.reshape(n, 2, n, 2, 3).transpose(0, 2, 3, 1, 4)          # [by, bx, dx, dy, rgb]: k = dx * 2 + dy (twiddled)
    V = blk.reshape(n * n, 12)
    wts = np.tile([0.30, 0.59, 0.11], 4) ** 0.5 * 1.6                    # luma-weighted distance
    Vw = V * wts
    rng = np.random.default_rng(seed)
    # k-means++ seeding on a sample, then Lloyd iterations on everything
    sample = Vw[rng.choice(len(Vw), 8192, replace=False)]
    C = [sample[rng.integers(len(sample))]]
    d2 = ((sample - C[0]) ** 2).sum(1)
    for _ in range(255):
        C.append(sample[rng.choice(len(sample), p=d2 / d2.sum())])
        d2 = np.minimum(d2, ((sample - C[-1]) ** 2).sum(1))
    C = np.array(C)
    def assign(C):
        best = np.empty(len(Vw), np.int64)
        for s in range(0, len(Vw), 8192):
            chunk = Vw[s:s + 8192]
            dd = (chunk ** 2).sum(1)[:, None] - 2 * chunk @ C.T + (C ** 2).sum(1)[None, :]
            best[s:s + 8192] = dd.argmin(1)
        return best
    for _ in range(iters):
        a = assign(C)
        cnt = np.bincount(a, minlength=256)
        S = np.zeros_like(C)
        np.add.at(S, a, Vw)
        empty = cnt == 0
        C[~empty] = S[~empty] / cnt[~empty, None]
        if empty.any():                                                  # re-seed dead codes on the worst blocks
            err = ((Vw - C[a]) ** 2).sum(1)
            C[empty] = Vw[np.argsort(err)[-empty.sum():]]
    # to RGB565, then the final assignment against the quantised codebook
    Crgb = np.clip(C / wts, 0, 255).reshape(256, 4, 3)
    code565 = pvrtex.pack(np.concatenate([Crgb, np.full((256, 4, 1), 255)], -1).round().astype(np.uint8), pvrtex.FMT_RGB565)
    Cq = pvrtex.unpack(code565, pvrtex.FMT_RGB565)[..., :3].astype(np.float64).reshape(256, 12) * wts
    idx = assign(Cq).reshape(n, n)
    return code565, idx


def vq_decode(code565, idx):
    n = idx.shape[0]
    rgb = pvrtex.unpack(code565, pvrtex.FMT_RGB565)[..., :3]             # (256, 4, 3)
    blk = rgb[idx]                                                        # (n, n, 4, 3), k = dx * 2 + dy
    return blk.reshape(n, n, 2, 2, 3).transpose(0, 3, 1, 2, 4).reshape(2 * n, 2 * n, 3)   # [by, dy, bx, dx]


def gnd_bytes(code565, idx, w, h):
    """GND1 file: 32-byte header + VQ data as the PVR reads it (2 KiB codebook, then the twiddled index map)."""
    n = idx.shape[0]
    tw = np.zeros(n * n, np.uint8)
    tw[pvrtex.twiddle_index(n, n).ravel()] = idx.ravel()
    data = code565.astype('<u2').tobytes() + tw.tobytes()
    outer = ((OUTER[0] >> 3) << 11) | ((OUTER[1] >> 2) << 5) | (OUTER[2] >> 3)
    hdr = struct.pack('<4sHHHHHHHHI8x', b'GND1', SIZE, SIZE, TPT, MARGIN, w, h, outer, 1, len(data))
    assert len(hdr) == 32
    return hdr + data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('map', nargs='?', default=os.path.join(ROOT, 'romdisk', 'maps', 'village.txt'))
    ap.add_argument('--preview', help='the decoded (VQ) texture as PNG')
    ap.add_argument('--raw', help='the painted texture before VQ as PNG')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    name, img, (w, h) = paint(a.map)
    code, idx = vq_encode(img)
    data = gnd_bytes(code, idx, w, h)
    out = os.path.join(os.path.dirname(a.map), name + '.gnd')
    dec = pvrtex.unpack(pvrtex.pack(np.concatenate([vq_decode(code, idx), np.full((SIZE, SIZE, 1), 255, np.uint8)], -1), pvrtex.FMT_RGB565), pvrtex.FMT_RGB565)[..., :3]
    raw565 = pvrtex.unpack(pvrtex.pack(np.concatenate([img.round().astype(np.uint8), np.full((SIZE, SIZE, 1), 255, np.uint8)], -1), pvrtex.FMT_RGB565), pvrtex.FMT_RGB565)[..., :3]
    mse = ((dec.astype(float) - raw565.astype(float)) ** 2).mean()
    psnr = 10 * np.log10(255 ** 2 / mse)
    if a.raw:
        Image.fromarray(img.round().astype(np.uint8)).save(a.raw)
    if a.preview:
        Image.fromarray(dec).save(a.preview)
    if a.check:
        same = os.path.exists(out) and open(out, 'rb').read() == data
        print('%s %s (VQ PSNR %.1f dB)' % ('ok  ' if same else 'DIFF', out, psnr))
        return 0 if same else 1
    open(out, 'wb').write(data)
    print('%s: %dx%d, %d texels per tile, %d-tile margin, VQ %d bytes (PSNR %.1f dB vs RGB565)' % (out, SIZE, SIZE, TPT, MARGIN, len(data), psnr))
    return 0


if __name__ == '__main__':
    sys.exit(main())
