"""bpy_decimate.py - quadric edge-collapse decimation of an outline hull (run with Blender's Python; hull_lab.py calls it).

    BLENDER_PY tools/cel/bpy_decimate.py IN.npz OUT.npz RATIO

IN: P (n,3) positions, B0/B1 (n,) bones (255 = none), W0 (n,) weight of B0 in 0..255, T (m,3) triangles (CCW).
The skin rides along as vertex groups (Blender's collapse interpolates them); OUT: the same arrays for the decimated
mesh, with N (k,3) Blender's smooth vertex normals and the two largest weights per vertex (renormalised).
"""
import sys
import bpy
import numpy as np

src, dst, ratio = sys.argv[-3], sys.argv[-2], float(sys.argv[-1])
d = np.load(src)
P, B0, B1, W0, T = d['P'], d['B0'], d['B1'], d['W0'], d['T']

bpy.ops.wm.read_factory_settings(use_empty=True)
me = bpy.data.meshes.new('hull')
me.from_pydata([tuple(map(float, p)) for p in P], [], [tuple(map(int, t)) for t in T])
me.update()
ob = bpy.data.objects.new('hull', me)
bpy.context.scene.collection.objects.link(ob)
bones = sorted(set(int(b) for b in B0) | set(int(b) for b in B1 if b != 255))
groups = {b: ob.vertex_groups.new(name='b%d' % b) for b in bones}
for i in range(len(P)):
    w = W0[i] / 255.0 if B1[i] != 255 else 1.0
    groups[int(B0[i])].add([i], float(w), 'REPLACE')
    if B1[i] != 255 and w < 1.0:
        groups[int(B1[i])].add([i], float(1.0 - w), 'ADD')
mod = ob.modifiers.new('dec', 'DECIMATE')
mod.decimate_type = 'COLLAPSE'
mod.ratio = ratio
mod.use_collapse_triangulate = True
bpy.context.view_layer.objects.active = ob
ob.select_set(True)
bpy.ops.object.modifier_apply(modifier='dec')
me = ob.data
me.update()
gname = {g.index: int(g.name[1:]) for g in ob.vertex_groups}
n = len(me.vertices)
P2 = np.array([tuple(v.co) for v in me.vertices], float)
N2 = np.array([tuple(v.normal) for v in me.vertices], float)
B0o = np.zeros(n, int); B1o = np.full(n, 255, int); W0o = np.full(n, 255, int)
for i, v in enumerate(me.vertices):
    ws = sorted(((g.weight, gname[g.group]) for g in v.groups if g.weight > 1e-4), reverse=True)[:2]
    if not ws:
        ws = [(1.0, bones[0])]
    B0o[i] = ws[0][1]
    if len(ws) == 2:
        s = ws[0][0] + ws[1][0]
        B1o[i] = ws[1][1]
        W0o[i] = int(round(255 * ws[0][0] / s))
T2 = np.array([tuple(p.vertices) for p in me.polygons if len(p.vertices) == 3], int)
np.savez(dst, P=P2, N=N2, B0=B0o, B1=B1o, W0=W0o, T=T2)
print('decimated %d -> %d vertices, %d -> %d triangles' % (len(P), n, len(T), len(T2)))
