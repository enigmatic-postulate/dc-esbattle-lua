"""convert_model.py - KayKit character or weapon FBX -> decimated .dmf for the Dreamcast.
Runs in Blender's Python (bpy 4.2). Writes a JSON report next to the output (or --report).

    $BPY tools/battle3d/convert_model.py --fbx ../KayKit/Characters/Skeleton_Rogue.fbx \
        --texture ../KayKit/Characters/skeleton_texture.png --tris 800 --out romdisk/models/skel_rogue.dmf
    $BPY tools/battle3d/convert_model.py --weapon --fbx ../KayKit/Weapons/Skeleton_Blade.fbx \
        --texture ../KayKit/Characters/skeleton_texture.png --tris 60 --out romdisk/models/w_skel_blade.dmf

Pipeline (per mesh part):
  1. into model space (characters: the armature space, Y up, facing +Z - the .dmf convention;
     weapons: the mesh's own Y-up frame, grip at the origin, blade along +Y = the hand slot's axes)
  2. planar dissolve (coplanar faces with the same UVs/material merge: free triangles, no visual
     change), triangulate, then edge-collapse decimation to the part's share of the budget
     (shares follow source triangle counts x --part-weight)
  3. skin weights capped to 2 bones (renormalised); parts without weights ride one bone (--rigid,
     default: hats/helmets/hoods/skulls -> head, capes -> chest)
  4. 'Glow' material parts (the skeletons' eyes) become an untextured batch coloured from the atlas
     (or --glow-color: the shipped skeletons use 9CF0FF, the project's eye colour since slice 3D-3)
  5. vertices deduplicated, sorted into skin groups; triangles stripified per batch (strips.py)
The texture is the KayKit atlas box-filtered to --tex-size (RGB565). Weapons keep the atlas UVs: at
runtime they are drawn with their holder's texture (no VRAM of their own).
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmfio  # noqa: E402
import strips as stripper  # noqa: E402

RIGID_DEFAULT = {'hat': 'head', 'helmet': 'head', 'helmetvisor': 'head', 'hood': 'head', 'skull': 'head', 'head': 'head',
                 'jaw': 'head', 'eyes': 'head', 'cape': 'chest', 'cloak': 'chest', 'beard': 'head'}
DOUBLE_SIDED = ('cape', 'cloak')


def mat_np(m):
    return np.array([[m[i][j] for j in range(4)] for i in range(4)], dtype=float)


def rgb565(img):
    a = np.asarray(img.convert('RGB'), np.uint32)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    return (((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)).astype(np.uint16)


def part_key(obj_name, fbx_base):
    n = obj_name
    if n.startswith(fbx_base + '_'):
        n = n[len(fbx_base) + 1:]
    return n


def decimated_mesh(bpy, obj, M, target_tris, dissolve_deg):
    """Copy of obj's mesh in model space, dissolved + triangulated + decimated to <= target_tris."""
    me = obj.data.copy()
    import mathutils
    me.transform(mathutils.Matrix([list(r) for r in M]))
    tmp = bpy.data.objects.new(obj.name + '_tmp', me)
    bpy.context.scene.collection.objects.link(tmp)
    for vg in obj.vertex_groups:
        tmp.vertex_groups.new(name=vg.name)
    src_tris = sum(len(p.vertices) - 2 for p in me.polygons)
    if dissolve_deg > 0:
        d = tmp.modifiers.new('dissolve', 'DECIMATE')
        d.decimate_type = 'DISSOLVE'
        d.angle_limit = np.radians(dissolve_deg)
        d.delimit = {'UV', 'MATERIAL', 'SEAM', 'SHARP'}
    tri = tmp.modifiers.new('tri', 'TRIANGULATE')
    tri.quad_method = 'BEAUTY'; tri.ngon_method = 'BEAUTY'
    dg = bpy.context.evaluated_depsgraph_get()
    ev = tmp.evaluated_get(dg)
    after_dissolve = sum(len(p.vertices) - 2 for p in ev.data.polygons)
    if after_dissolve > target_tris:
        c = tmp.modifiers.new('collapse', 'DECIMATE')
        c.decimate_type = 'COLLAPSE'
        c.use_collapse_triangulate = True
        # the modifier's ratio is approximate: bisect it to land on the budget
        lo, hi = 0.01, 1.0
        for _ in range(14):
            c.ratio = (lo + hi) / 2
            dg = bpy.context.evaluated_depsgraph_get()
            n = sum(len(p.vertices) - 2 for p in tmp.evaluated_get(dg).data.polygons)
            if n > target_tris:
                hi = c.ratio
            else:
                lo = c.ratio
        c.ratio = lo
    dg = bpy.context.evaluated_depsgraph_get()
    ev = tmp.evaluated_get(dg)
    out = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
    out_obj = bpy.data.objects.new(obj.name + '_dec', out)
    bpy.context.scene.collection.objects.link(out_obj)
    for vg in obj.vertex_groups:
        out_obj.vertex_groups.new(name=vg.name)
    return out_obj, src_tris, after_dissolve


def convert(a):
    import bpy
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=a.fbx)
    base = os.path.splitext(os.path.basename(a.fbx))[0]
    arm = None if a.weapon else [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    meshes = [o for o in bpy.data.objects if o.type == 'MESH' and part_key(o.name, base).lower() not in [d.lower() for d in a.drop]]
    bone_names = [b.name for b in arm.data.bones] if arm else []
    rigid = dict(RIGID_DEFAULT)
    for r in a.rigid:
        k, v = r.split('='); rigid[k.lower()] = v
    weights = {}
    for w in a.part_weight:
        k, v = w.split('='); weights[k.lower()] = float(v)
    src = {o.name: sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes}
    share = {o.name: src[o.name] * weights.get(part_key(o.name, base).lower(), 1.0) for o in meshes}
    tot = sum(share.values())
    from PIL import Image
    atlas = Image.open(a.texture).convert('RGB') if a.texture else None
    atlas_np = np.asarray(atlas) if atlas else None

    verts, index, tris_by_mat = [], {}, {}
    report = dict(source=os.path.basename(a.fbx), budget=a.tris, parts=[])
    for o in meshes:
        pk = part_key(o.name, base).lower()
        M = np.linalg.inv(mat_np(arm.matrix_world)) @ mat_np(o.matrix_world) if arm else np.eye(4)
        target = max(8, int(round(a.tris * share[o.name] / tot)))
        dec, n_src, n_diss = decimated_mesh(bpy, o, M, target, a.dissolve)
        me = dec.data
        me.calc_loop_triangles()
        uvl = me.uv_layers.active.data if me.uv_layers.active else None
        cn = me.corner_normals
        glow = any(mt is not None and mt.name.lower() == 'glow' for mt in me.materials)
        mat = 1 if glow else 0
        double = any(pk.startswith(d) for d in DOUBLE_SIDED)
        batch_key = (mat, double)
        gname = {g.index: g.name for g in dec.vertex_groups}
        n_out = 0
        for lt in me.loop_triangles:
            corner = []
            for li in lt.loops:
                vi = me.loops[li].vertex_index
                co = np.array(me.vertices[vi].co[:])
                nrm = np.array(cn[li].vector[:])
                u, v = (uvl[li].uv[0], 1.0 - uvl[li].uv[1]) if uvl else (0.0, 0.0)
                if arm:
                    ws = sorted(((g.weight, gname[g.group]) for g in me.vertices[vi].groups
                                 if g.weight > 1e-4 and gname.get(g.group) in bone_names), reverse=True)[:2]
                    if not ws:
                        rb = rigid.get(pk)
                        assert rb, 'part %s has no weights: give --rigid %s=<bone>' % (o.name, pk)
                        ws = [(1.0, rb)]
                    if len(ws) == 2 and ws[1][0] < a.min_weight * (ws[0][0] + ws[1][0]):
                        ws = ws[:1]
                    b0 = bone_names.index(ws[0][1])
                    if len(ws) == 2:
                        b1 = bone_names.index(ws[1][1]); w0 = int(round(255 * ws[0][0] / (ws[0][0] + ws[1][0])))
                        if w0 >= 255: b1, w0 = dmfio.NO_BONE, 255
                    else:
                        b1, w0 = dmfio.NO_BONE, 255
                else:
                    b0, b1, w0 = dmfio.NO_BONE, dmfio.NO_BONE, 255
                argb = 0xFFFFFFFF
                if glow and a.glow_color:
                    argb = 0xFF000000 | int(a.glow_color, 16)
                elif glow and atlas_np is not None:
                    h_, w_ = atlas_np.shape[:2]
                    px = atlas_np[min(h_ - 1, max(0, int(v * h_))), min(w_ - 1, max(0, int(u * w_)))]
                    argb = 0xFF000000 | int(px[0]) << 16 | int(px[1]) << 8 | int(px[2])
                n8 = np.clip(np.round(nrm / max(np.linalg.norm(nrm), 1e-9) * 127), -127, 127).astype(int)
                key = (tuple(np.round(co, 5)), tuple(n8), round(u, 5), round(v, 5), b0, b1, w0, argb, batch_key)
                if key not in index:
                    index[key] = len(verts)
                    verts.append(dict(p=co, n=n8, u=u, v=v, b0=b0, b1=b1, w0=w0, argb=argb))
                corner.append(index[key])
            tris_by_mat.setdefault(batch_key, []).append(tuple(corner))
            n_out += 1
        report['parts'].append(dict(part=pk, source_tris=n_src, after_dissolve=n_diss, tris=n_out, target=target,
                                    glow=glow, double_sided=double))
        bpy.data.objects.remove(dec)

    # skin groups: vertices sorted by bone pair (stable)
    order = sorted(range(len(verts)), key=lambda i: (verts[i]['b0'], verts[i]['b1'], i))
    remap = {old: new for new, old in enumerate(order)}
    vt = np.zeros(len(verts), dmfio.VTX)
    groups = []
    for new, old in enumerate(order):
        vv = verts[old]
        vt[new] = (vv['p'][0], vv['p'][1], vv['p'][2], vv['u'], vv['v'], vv['n'][0], vv['n'][1], vv['n'][2], vv['w0'], vv['argb'], b'\0' * 4)
        pair = (vv['b0'], vv['b1'])
        if groups and tuple(groups[-1][2:]) == pair:
            groups[-1][1] += 1
        else:
            groups.append([new, 1, pair[0], pair[1]])
    ga = np.zeros(len(groups), dmfio.GRP)
    for i, g in enumerate(groups):
        ga[i] = (g[0], g[1], g[2], g[3], 0)
    # batches
    words, batches, materials = [], [], []
    mat_index = {}
    for (mat, double) in sorted(tris_by_mat):
        if mat not in mat_index:
            mat_index[mat] = len(materials)
            materials.append(dict(tex='@', flags=dmfio.MAT_BILINEAR) if mat == 0 else dict(tex='', flags=0))
        tris = [tuple(remap[i] for i in t) for t in tris_by_mat[(mat, double)]]
        st = stripper.stripify(tris)
        assert stripper.check(tris, st)
        first = len(words)
        for s in st:
            words.append(len(s)); words.extend(s)
        batches.append(dict(first_word=first, num_words=len(words) - first, num_strips=len(st),
                            flags=(dmfio.BATCH_DOUBLE_SIDED if double else 0) | (mat_index[mat] << 8), num_tris=len(tris)))
    if not a.texture:
        materials = [dict(tex='', flags=0)]
    bones = []
    if arm:
        for i, b in enumerate(arm.data.bones):
            rel = mat_np(b.matrix_local)
            if b.parent is not None:
                rel = np.linalg.inv(mat_np(b.parent.matrix_local)) @ rel
            import rig_sample as rs
            bones.append(dict(name=b.name, parent=bone_names.index(b.parent.name) if b.parent else -1,
                              t=rel[:3, 3], q=rs.mat_to_quat(rel[:3, :3]), inv_bind=np.linalg.inv(mat_np(b.matrix_local))[:3, :]))
    texture = None
    if a.texture:
        img = atlas.resize((a.tex_size, a.tex_size), Image.BOX)
        texture = dict(w=a.tex_size, h=a.tex_size, format=dmfio.TEX_RGB565, filter=1, texels=rgb565(img))
    data = dmfio.write_dmf(vt, ga, batches, np.array(words, '<u2'), bones, [], materials, texture)
    open(a.out, 'wb').write(data)
    ntris = sum(b['num_tris'] for b in batches)
    emitted = sum(b['num_words'] - b['num_strips'] for b in batches)
    report.update(dict(out=os.path.basename(a.out), tris=ntris, verts=len(vt), groups=len(ga), strips=sum(b['num_strips'] for b in batches),
                       verts_per_tri=round(emitted / max(1, ntris), 3), bytes=len(data),
                       source_tris=sum(p['source_tris'] for p in report['parts'])))
    rp = a.report or os.path.splitext(a.out)[0] + '.json'
    os.makedirs(os.path.dirname(os.path.abspath(rp)), exist_ok=True)
    json.dump(report, open(rp, 'w'), indent=1)
    print('%s: %d -> %d tris, %d verts, %d groups, %.2f verts/tri emitted, %d bytes' % (
        a.out, report['source_tris'], ntris, len(vt), len(ga), report['verts_per_tri'], len(data)))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--fbx', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--texture')
    ap.add_argument('--tris', type=int, required=True)
    ap.add_argument('--weapon', action='store_true')
    ap.add_argument('--tex-size', type=int, default=64)
    ap.add_argument('--dissolve', type=float, default=1.0, help='planar dissolve angle (degrees), 0 = off')
    ap.add_argument('--min-weight', type=float, default=0.15, help='drop a second bone below this share')
    ap.add_argument('--drop', action='append', default=[])
    ap.add_argument('--rigid', action='append', default=[])
    ap.add_argument('--part-weight', action='append', default=[])
    ap.add_argument('--glow-color', help="RRGGBB for the 'Glow' parts (the project's skeleton eyes: 9CF0FF)")
    ap.add_argument('--report')
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    convert(ap.parse_args(args))
