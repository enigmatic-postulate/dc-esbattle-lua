"""view_cull.py - battle variants of static props with only the triangles a battle camera can see
(slice 3D-4; the same idea as the field's <model>_r<yaw>.dmf, for the battle cameras). Host tool.

    python3 tools/battle3d/view_cull.py romdisk/arenas/village.txt house_stone_b cams.txt romdisk/models/house_stone_b.dmf \
        --source house_stone_r0      (the arena places house_stone_b; its triangles come from house_stone_r0)

Translucent batches (the vines: alpha-tested leaves) never occlude: their triangles are tested
against the opaque ones only.
cams.txt: one camera per line, "ex,ey,ez tx,ty,tz fov" - the camera of every frame of bot-played
battles (trace_main --view B lines; tests/battle3d/cameras.sh collects them). Every camera is also
tried with its eye moved by +-JITTER units on each axis, at twice the game's resolution, so a
triangle that covers less than a pixel from a sampled camera still counts. Only the prop itself
occludes (never other props or fighters, which move). No vertex moves: from every sampled camera
the result draws the same pixels as the original.
"""
import argparse
import ctypes
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmfio  # noqa: E402
import preview  # noqa: E402
import raster  # noqa: E402
import strips as stripper  # noqa: E402

JITTER = 0.3
SCALE = 2


def prop_place(arena, model):
    for ln in open(arena):
        p = ln.split()
        if len(p) >= 5 and p[0] == 'prop' and p[1] == model:
            sc = float(p[p.index('scale') + 1]) if 'scale' in p else 1.0
            return float(p[2]), float(p[3]), float(p[4]), sc
    raise SystemExit('prop %s not in %s' % (model, arena))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('arena'); ap.add_argument('model'); ap.add_argument('cams'); ap.add_argument('out')
    ap.add_argument('--every', type=int, default=1, help='use every Nth distinct camera')
    ap.add_argument('--source', help='source model (default: the placed model itself)')
    a = ap.parse_args()
    m = dmfio.read_dmf(os.path.join(os.path.dirname(a.arena), '..', 'models', (a.source or a.model) + '.dmf'))
    x, z, rot, sc = prop_place(a.arena, a.model)
    world = preview.trs_y((x, 0, z), np.radians(rot), sc)
    v = m['verts']
    P = np.stack([v['x'], v['y'], v['z'], np.ones(len(v))], 1) @ world.T
    tris, tri_batch = [], []
    for k in range(len(m['batches'])):
        for s in dmfio.strips_of_batch(m, k):
            for t in dmfio.strip_triangles(s):
                tris.append(t); tri_batch.append(k)
    tris = np.array(tris)
    mats = m['materials']
    tri_alpha = np.array([bool(mats and mats[m['batches'][k]['flags'] >> 8]['flags'] & dmfio.MAT_ALPHA) for k in tri_batch])
    cams = sorted(set(ln.strip() for ln in open(a.cams) if ln.strip()))[::a.every]
    W, H = 640 * SCALE, 480 * SCALE
    seen = np.zeros(len(tris), bool)
    lib = raster._lib()
    lib.raster_ids.restype = None
    lib.raster_vis.restype = None
    n_views = 0
    for c in cams:
        e, t, fov = c.split()
        eye = np.array([float(q) for q in e.split(',')]); tgt = np.array([float(q) for q in t.split(',')])
        for j in ([0, 0, 0], [JITTER, 0, 0], [-JITTER, 0, 0], [0, JITTER, 0], [0, -JITTER, 0], [0, 0, JITTER], [0, 0, -JITTER]):
            cam = raster.Camera(eye + np.array(j), tgt, float(fov), W, H)
            S, Wd = cam.project(P[:, :3])
            keep = (Wd[tris] > cam.near_w).all(1)
            depth = np.zeros(W * H, np.float32)
            for pass_alpha in (False, True):
                idx = np.nonzero(keep & (tri_alpha == pass_alpha))[0]
                if len(idx) == 0:
                    continue
                rv = np.zeros(len(idx) * 3, raster.RV)
                sel = tris[idx].reshape(-1)
                rv['x'], rv['y'], rv['z'] = S[sel, 0], S[sel, 1], S[sel, 2]
                if not pass_alpha:           # opaque: nearest triangle per pixel, writes depth
                    ids = np.full(W * H, -1, np.int32)
                    lib.raster_ids(rv.ctypes.data_as(ctypes.c_void_p), ctypes.c_int(len(idx)), ctypes.c_int(0), ctypes.c_int(0),
                                   ids.ctypes.data_as(ctypes.c_void_p), depth.ctypes.data_as(ctypes.c_void_p), ctypes.c_int(W), ctypes.c_int(H))
                    u = np.unique(ids[ids >= 0])
                    seen[idx[u]] = True
                else:                        # translucent: any pixel in front of the opaque depth
                    vis = np.zeros(len(idx), np.uint8)
                    lib.raster_vis(rv.ctypes.data_as(ctypes.c_void_p), ctypes.c_int(len(idx)), depth.ctypes.data_as(ctypes.c_void_p),
                                   vis.ctypes.data_as(ctypes.c_void_p), ctypes.c_int(W), ctypes.c_int(H))
                    seen[idx[vis > 0]] = True
            n_views += 1
    # rebuild: the same vertices, only the seen triangles, restripped per batch
    words, batches = [], []
    for k, b in enumerate(m['batches']):
        tt = [tuple(int(q) for q in tris[i]) for i in range(len(tris)) if tri_batch[i] == k and seen[i]]
        if not tt:
            continue
        st = stripper.stripify(tt)
        assert stripper.check(tt, st)
        first = len(words)
        for s in st:
            words.append(len(s)); words.extend(s)
        batches.append(dict(first_word=first, num_words=len(words) - first, num_strips=len(st), flags=b['flags'], num_tris=len(tt)))
    # drop vertices no kept triangle uses
    refs = np.zeros(len(v), bool)
    i = 0
    while i < len(words):
        n = words[i]; refs[words[i + 1:i + 1 + n]] = True; i += 1 + n
    remap = -np.ones(len(v), int); remap[refs] = np.arange(refs.sum())
    i = 0
    while i < len(words):
        n = words[i]
        for j in range(i + 1, i + 1 + n): words[j] = int(remap[words[j]])
        i += 1 + n
    vt = v[refs]
    groups = np.zeros(1, dmfio.GRP); groups[0] = (0, len(vt), dmfio.NO_BONE, dmfio.NO_BONE, 0)
    data = dmfio.write_dmf(vt, groups, batches, np.array(words, '<u2'), [], [], m['materials'], m['texture'], m['version'])
    open(a.out, 'wb').write(data)
    src_out = sum(b['num_words'] - b['num_strips'] for b in m['batches'])
    out_v = sum(b['num_words'] - b['num_strips'] for b in batches)
    print('%s: %d cameras x 7 (%d views): %d of %d triangles seen, strip vertices %d -> %d' % (
        os.path.basename(a.out), len(cams), n_views, int(seen.sum()), len(tris), src_out, out_v))


if __name__ == '__main__':
    main()
