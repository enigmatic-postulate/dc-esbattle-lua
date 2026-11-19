#!/usr/bin/env python3
"""hull_lab.py - offline experiment for the cel-shading study (docs/CEL_SHADING_STUDY.md, section 5): how cheap can an
outline hull get? For a character .dmf it builds

  full     - the hull the runtime prototype draws today (every vertex and strip of the model's opaque batches);
  welded   - the same surface with the vertices merged by position + skin (a hull needs no UVs or colours), re-stripped;
  cN       - welded, then decimated by vertex clustering (cells of N model units, never across bones), re-stripped;

and reports each one's per-frame cost in the units of the SH4 estimate (xf = vertices transformed, out = vertices
written; 40 xf + 24 out instructions before the 1.52 calibration), then renders the model + each hull (pushed out by
`px` pixels along its smoothed normals, front faces culled, black - as the runtime does) for a visual check.

    python3 tools/cel/hull_lab.py knight --px 1.5 --sheet docs/cel/hull_lab_knight.jpg [--cells 0.04,0.07,0.1]
"""
import argparse, os, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
import dmfio, preview, raster, strips  # noqa: E402


def opaque_tris(m):
    tris, out = [], 0
    for k, bt in enumerate(m['batches']):
        if bt['flags'] & 2:                       # BATCH_TRANSLUCENT: no hull
            continue
        out += bt['num_words'] - bt['num_strips']
        for s in dmfio.strips_of_batch(m, k):
            tris += dmfio.strip_triangles(s)
    return tris, out


def skin_of(m):
    nv = len(m['verts'])
    b0 = np.zeros(nv, int); b1 = np.full(nv, 255, int)
    for g in m['groups']:
        f, c = int(g['first']), int(g['count'])
        b0[f:f + c] = g['bone0']; b1[f:f + c] = g['bone1']
    return b0, b1, m['verts']['w0'].astype(int)


def welded(m):
    """(P, N, b0, b1, w0, tris): unique (position, skin) vertices with smoothed normals; duplicate faces removed."""
    v = m['verts']
    b0, b1, w0 = skin_of(m)
    key, remap = {}, np.zeros(len(v), int)
    for i in range(len(v)):
        k = (round(float(v['x'][i]) * 2048), round(float(v['y'][i]) * 2048), round(float(v['z'][i]) * 2048), b0[i], b1[i], w0[i])
        remap[i] = key.setdefault(k, len(key))
    n = len(key)
    P = np.zeros((n, 3)); N = np.zeros((n, 3)); B0 = np.zeros(n, int); B1 = np.zeros(n, int); W0 = np.zeros(n, int)
    for i in range(len(v)):
        j = remap[i]
        P[j] = (v['x'][i], v['y'][i], v['z'][i]); N[j] += (v['nx'][i], v['ny'][i], v['nz'][i])
        B0[j], B1[j], W0[j] = b0[i], b1[i], w0[i]
    # smooth across skin splits too (vertices at one position with different skins share a normal)
    pos = {}
    for j in range(n):
        pos.setdefault(tuple(np.round(P[j] * 2048).astype(int)), []).append(j)
    for js in pos.values():
        s = N[js].sum(0)
        for j in js:
            N[j] = s
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
    tris, seen = [], set()
    for a, b, c in opaque_tris(m)[0]:
        t = (remap[a], remap[b], remap[c])
        if len(set(t)) < 3:
            continue
        r = min((t, (t[1], t[2], t[0]), (t[2], t[0], t[1])))
        if r in seen:
            continue
        seen.add(r); tris.append(t)
    return P, N, B0, B1, W0, tris


def decimate(P, N, B0, B1, W0, tris, cell):
    """Vertex clustering within each dominant bone: one representative (the member nearest the cluster's mean) per
    cell; its normal = the members' mean normal. Degenerate and duplicate triangles are dropped."""
    dom = np.where(W0 >= 128, B0, np.where(B1 == 255, B0, B1))
    keys = {}
    cl = np.zeros(len(P), int)
    for i in range(len(P)):
        k = (dom[i],) + tuple(np.floor(P[i] / cell).astype(int))
        cl[i] = keys.setdefault(k, len(keys))
    nc = len(keys)
    rep = np.zeros(nc, int); Nn = np.zeros((nc, 3))
    for c in range(nc):
        mem = np.nonzero(cl == c)[0]
        mu = P[mem].mean(0)
        rep[c] = mem[np.argmin(((P[mem] - mu) ** 2).sum(1))]
        Nn[c] = N[mem].sum(0)
    Nn /= np.maximum(np.linalg.norm(Nn, axis=1, keepdims=True), 1e-9)
    out, seen = [], set()
    for a, b, c in tris:
        t = (cl[a], cl[b], cl[c])
        if len(set(t)) < 3:
            continue
        r = min((t, (t[1], t[2], t[0]), (t[2], t[0], t[1])))
        if r in seen:
            continue
        seen.add(r); out.append(t)
    return P[rep], Nn, B0[rep], B1[rep], W0[rep], out


def decimate_quadric(P, N, B0, B1, W0, tris, ratio):
    """Blender's quadric edge collapse (tools/cel/bpy_decimate.py, run with $BLENDER_PY) on the hull welded by position
    only (one connected surface; the first skin at a position wins), skin weights carried as vertex groups."""
    import subprocess, tempfile
    key, first = {}, []
    remap = np.zeros(len(P), int)
    for i in range(len(P)):
        k = tuple(np.round(P[i] * 2048).astype(int))
        if k not in key:
            key[k] = len(first); first.append(i)
        remap[i] = key[k]
    first = np.array(first)
    T, seen = [], set()
    for a, b, c in tris:
        t = (remap[a], remap[b], remap[c])
        r = min((t, (t[1], t[2], t[0]), (t[2], t[0], t[1])))
        if len(set(t)) == 3 and r not in seen:
            seen.add(r); T.append(t)
    d = tempfile.mkdtemp(prefix='hull_q_')
    np.savez(os.path.join(d, 'in.npz'), P=P[first], B0=B0[first], B1=B1[first], W0=W0[first], T=np.array(T, int))
    py = os.environ.get('BLENDER_PY', '/tmp/bpyenv/bin/python')
    subprocess.run([py, os.path.join(HERE, 'bpy_decimate.py'), os.path.join(d, 'in.npz'), os.path.join(d, 'out.npz'), str(ratio)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    o = np.load(os.path.join(d, 'out.npz'))
    N2 = o['N'] / np.maximum(np.linalg.norm(o['N'], axis=1, keepdims=True), 1e-9)
    return o['P'], N2, o['B0'], o['B1'], o['W0'], [tuple(t) for t in o['T']]


def cost(nv, tris):
    st = strips.stripify(tris)
    out = sum(len(s) for s in st)
    return nv, out, len(tris)


def skin_points(P, N, B0, B1, W0, skin, k):
    """The runtime's hull: offset in bind space, then skinned (two-bone blend by w0)."""
    Q = np.c_[P + N * k, np.ones(len(P))]
    out = np.zeros((len(P), 3))
    for i in range(len(P)):
        A = skin[B0[i]]
        p = A @ Q[i]
        if B1[i] != 255:
            w = W0[i] / 255.0
            p = p * w + (skin[B1[i]] @ Q[i]) * (1 - w)
        out[i] = p[:3]
    return out


def write_hull(m, hull, path):
    """The hull as its own skinned .dmf: same skeleton (bone order), vertices sorted into skin groups, smoothed normals
    in nx/ny/nz (and in the pad bytes, where the runtime's hull pass reads them), one untextured batch of strips."""
    P, N, B0, B1, W0, tris = hull
    order = sorted(range(len(P)), key=lambda i: (B0[i], B1[i], i))
    new = {o: i for i, o in enumerate(order)}
    v = np.zeros(len(P), dmfio.VTX)
    for i, o in enumerate(order):
        n8 = np.clip(np.round(N[o] * 127), -127, 127).astype(int)
        v[i]['x'], v[i]['y'], v[i]['z'] = P[o]
        v[i]['nx'], v[i]['ny'], v[i]['nz'] = n8
        v[i]['w0'] = W0[o] if B1[o] != 255 else 255
        v[i]['argb'] = 0xFF000000
        v[i]['pad'] = bytes([n8[0] & 255, n8[1] & 255, n8[2] & 255, 0])
    groups = []
    for i, o in enumerate(order):
        k = (B0[o], B1[o])
        if groups and (groups[-1][2], groups[-1][3]) == k:
            groups[-1][1] += 1
        else:
            groups.append([i, 1, k[0], k[1]])
    g = np.zeros(len(groups), dmfio.GRP)
    for j, (f, c, b0, b1) in enumerate(groups):
        g[j] = (f, c, b0, b1, 0)
    st = strips.stripify([(new[a], new[b], new[c]) for a, b, c in tris])
    words = []
    for s_ in st:
        words.append(len(s_)); words += s_
    batches = [dict(first_word=0, num_words=len(words), num_strips=len(st), flags=0, num_tris=len(tris))]
    data = dmfio.write_dmf(v, g, batches, np.array(words, '<u2'), bones=m['bones'], materials=[dict(tex='', flags=0)])
    open(path, 'wb').write(data)
    return len(data)


def render(m, hull, qs, ts, px, eye, target, size, fov=40, zpush=0.0):
    c = raster.Canvas(size[0], size[1], 0x5A7A48)
    cam = raster.Camera(eye, target, fov, size[0], size[1])
    world = preview.trs_y((0, 0, 0), 0.5)
    skin = dmfio.skin_mats(m, qs, ts)
    preview.draw_model(c, cam, m, world, skin)
    if hull is not None:
        P, N, B0, B1, W0, tris = hull
        Wc = -(cam.view @ np.r_[world[:3, 3] + np.array([0, 0.9, 0]), 1.0])[2]
        k = px * Wc / cam.focal                       # model units (scale 1)
        Pw = skin_points(P, N, B0, B1, W0, skin, k)
        Pw = (np.c_[Pw, np.ones(len(Pw))] @ world.T)[:, :3]
        S, W = cam.project(Pw)
        rv = np.zeros(len(P), raster.RV)
        rv['x'], rv['y'], rv['z'] = S[:, 0], S[:, 1], S[:, 2] / (1.0 + zpush * S[:, 2])   # depth push by zpush world units: 1/(W + d)
        rv['argb'] = 0xFF140C10
        idx = np.array(tris)
        c.tris(rv[idx.reshape(-1)], None, False, raster.OP, 2)
    return c.image()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model')
    ap.add_argument('--px', type=float, default=1.5)
    ap.add_argument('--cells', default='0.04,0.07,0.10')
    ap.add_argument('--quadric', default='', help="Blender quadric-collapse ratios, e.g. 0.2,0.15 (variants 'qR')")
    ap.add_argument('--sheet')
    ap.add_argument('--clip', default='idle_a@0.5')
    ap.add_argument('--write', help='write the hull of the first --cells value to this directory as <model>.dmf')
    ap.add_argument('--zpush', type=float, default=0.0, help='hull depth push in model units: z = 1/(W + zpush)')
    a = ap.parse_args()
    m = dmfio.read_dmf(os.path.join(ROOT, 'romdisk', 'models', a.model + '.dmf'))
    nv = len(m['verts'])
    ftris, fout = opaque_tris(m)
    base = 40 * nv + 24 * fout
    W = welded(m)
    variants = [('full', None, (nv, fout, len(ftris))), ('welded', W, cost(len(W[0]), W[5]))]
    for c in [float(x) for x in a.cells.split(',') if x]:
        D = decimate(*W, c)
        variants.append(('c%.2f' % c, D, cost(len(D[0]), D[5])))
    for r in [float(x) for x in a.quadric.split(',') if x]:
        D = decimate_quadric(*W, r)
        variants.append(('q%.2f' % r, D, cost(len(D[0]), D[5])))
    print('%s: %d vertices, %d opaque triangles' % (a.model, nv, len(ftris)))
    for name, _, (x, o, t) in variants:
        w = 40 * x + 24 * o
        print('  %-8s xf %5d  out %5d  tris %5d   work %6d = %3.0f %% of full   (+%.3f M est per copy on screen)' % (
            name, x, o, t, w, 100.0 * w / base, 1.52 * w / 1e6))
    if a.write:
        os.makedirs(a.write, exist_ok=True)
        path = os.path.join(a.write, a.model + '.dmf')
        print('wrote %s (%d bytes, %s)' % (path, write_hull(m, variants[2][1], path), variants[2][0]))   # the first decimated variant
    if not a.sheet:
        return
    lib = dmfio.read_dma(os.path.join(ROOT, 'romdisk', 'anims', 'rig_medium.dma'))
    clip, t = a.clip.split('@')
    qs, ts = preview.pose_for(m, lib, clip, float(t))
    views = [('close (dialog)', (0.0, 1.5, 3.2), (0, 0.8, 0), (320, 320)), ('field distance', (0.0, 7.0, 9.5), (0, 0.7, 0), (320, 320))]
    cols = [('no outline', None)] + [(n, v if v is not None else W) for n, v, _ in variants]
    # 'full' draws the welded surface with every vertex (the same silhouette as the runtime's full hull)
    cell = 320
    sheet = Image.new('RGB', (cell * len(cols), 20 + (cell + 18) * len(views)), (16, 16, 20))
    dr = ImageDraw.Draw(sheet)
    for j, (n, _) in enumerate(cols):
        info = '' if j == 0 else ' (%d%%)' % round(100.0 * (40 * variants[j - 1][2][0] + 24 * variants[j - 1][2][1]) / base)
        dr.text((j * cell + 6, 4), n + info, fill=(255, 230, 160))
    for i, (vn, eye, tgt, size) in enumerate(views):
        y = 20 + i * (cell + 18)
        dr.text((6, y + 2), vn, fill=(200, 200, 200))
        for j, (n, h) in enumerate(cols):
            im = render(m, h, qs, ts, a.px, eye, tgt, (640, 640) if i == 1 else size, zpush=a.zpush)
            if i == 1:                                  # field distance: crop the character, 2x
                im = im.crop((160, 160, 480, 480))
            sheet.paste(im.resize((cell, cell), Image.LANCZOS), (j * cell, y + 18))
    sheet.save(a.sheet, quality=90)
    print('wrote', a.sheet)


if __name__ == '__main__':
    main()
