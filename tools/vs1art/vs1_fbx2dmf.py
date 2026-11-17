"""VENDORED from the VS1 lineage (larp3-2/tools/dmf/fbx2dmf.py), unchanged below this note. Its character
converter (assimp + ufbx + meshoptimizer: hidden-surface pass, error-bounded simplification per material and
per part, 128x128 textures). tools/vs1art/build_chars.sh runs it for the KayKit characters VS1 did not ship
(Barbarian, Skeleton_Mage, Skeleton_Rogue) with VS1's own recipe; needs Python >= 3.12 and
tools/vs1art/requirements-vs1.txt.
"""
#!/usr/bin/env python3
"""fbx2dmf - converts a skinned KayKit character FBX (+ texture, + optional Mixamo clips) to DMF.

  python3 tools/dmf/fbx2dmf.py --model Knight.fbx --texture knight_texture.png \
      --anim Walking.fbx:walk --target-tris 2000 -o romdisk/models/hero.dmf

Pipeline (each step prints what it did):
  1. load mesh parts, skeleton and bind pose (assimp); merge parts into one model-space mesh
  2. skin weights: keep the 2 strongest influences per vertex, renormalise, snap weak ones to rigid
  3. hidden-surface pass: drop triangles that no view of the bind pose / animation ever shows
     (e.g. the Knight's head inside the helmet)
  4. quadric simplification to --target-tris (meshoptimizer, UV seams + normals as attributes)
  5. group vertices by bone pair, stripify (meshoptimizer), remap indices
  6. texture: box-downsample to --tex-size, pack RGB565, twiddle
  7. clips: retarget Mixamo animation onto the KayKit rig (world-space rest-delta), strip root motion
See docs/DMF.md for the file format and the runtime side.
"""
import argparse
import ctypes
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmf_format as D          # noqa: E402
import pvrtex                   # noqa: E402
import raster                   # noqa: E402

# Mixamo -> KayKit "Rig_Medium" bone map (target: source). Unmapped target bones keep their rest
# local rotation (hand.*, handslot.*, root).
KAYKIT_FROM_MIXAMO = {
    "hips": "Hips", "spine": "Spine", "chest": "Spine2", "head": "Head",
    "upperarm.l": "LeftArm", "lowerarm.l": "LeftForeArm", "wrist.l": "LeftHand",
    "upperarm.r": "RightArm", "lowerarm.r": "RightForeArm", "wrist.r": "RightHand",
    "upperleg.l": "LeftUpLeg", "lowerleg.l": "LeftLeg", "foot.l": "LeftFoot", "toes.l": "LeftToeBase",
    "upperleg.r": "RightUpLeg", "lowerleg.r": "RightLeg", "foot.r": "RightFoot", "toes.r": "RightToeBase",
}


# ============================================================================ math helpers
def quat_from_mat(m):
    m = np.asarray(m, np.float64)[:3, :3]
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def mat_from_quat(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def orthonormalize(r):
    u, _, vt = np.linalg.svd(r[:3, :3])
    o = u @ vt
    if np.linalg.det(o) < 0:
        u[:, -1] *= -1
        o = u @ vt
    return o


def trs(rot3, pos):
    m = np.eye(4)
    m[:3, :3] = rot3
    m[:3, 3] = pos
    return m


# ============================================================================ 1. load (assimp)
def load_character(path):
    import assimp_py as assimp
    scene = assimp.import_file(path, assimp.Process_Triangulate | assimp.Process_JoinIdenticalVertices)
    nodes = {}           # name -> dict(local, world, parent, children)

    def walk(n, parent, parent_world):
        local = np.array(n.transformation, np.float64).reshape(4, 4)
        world = parent_world @ local
        nodes[n.name] = dict(local=local, world=world, parent=parent, meshes=list(n.mesh_indices),
                             children=[c.name for c in n.children])
        for c in n.children:
            walk(c, n.name, world)

    walk(scene.root_node, None, np.eye(4))
    root_name = scene.root_node.name

    # skeleton = every node referenced by a skin + all of its ancestors (minus the scene root)
    skin_bones = {b.name for m in scene.meshes for b in (m.bones or [])}
    keep = set()
    for b in skin_bones:
        n = b
        while n is not None and n != root_name:
            keep.add(n)
            n = nodes[n]["parent"]
    order = []

    def dfs(name):
        if name in keep:
            order.append(name)
        for c in nodes[name]["children"]:
            dfs(c)

    dfs(root_name)
    bone_index = {n: i for i, n in enumerate(order)}
    if len(order) > D.MAX_BONES:
        sys.exit(f"error: {len(order)} bones > DMF limit {D.MAX_BONES}")

    bones = np.zeros(len(order), D.BONE_DT)
    for i, name in enumerate(order):
        nd = nodes[name]
        p = nd["parent"]
        # bones directly under the scene root fold the root transform into their rest local
        local = nd["local"] if p in bone_index else nd["world"]
        sc = np.linalg.norm(local[:3, :3], axis=0)
        if np.abs(sc - 1).max() > 1e-3:
            print(f"  warning: bone {name} has scale {sc}; DMF bones are rigid (scale dropped)")
        inv_bind = np.linalg.inv(nd["world"])
        bones[i] = (name.encode()[:15], bone_index.get(p, -1), 0, inv_bind[:3, :].ravel(),
                    quat_from_mat(orthonormalize(local)), local[:3, 3])

    # merge mesh parts (model space = bind-pose world space)
    P, N, UV, WB, WW, TRI, PART, VMAT = [], [], [], [], [], [], [], []
    mats = []
    base = 0
    for mi, m in enumerate(scene.meshes):
        owner = next(k for k, v in nodes.items() if mi in v["meshes"])
        M = nodes[owner]["world"]
        nv = m.num_vertices
        pos = np.array(m.vertices, np.float64).reshape(nv, 3)
        nrm = np.array(m.normals, np.float64).reshape(nv, 3)
        ncomp = m.num_uv_components[0] if m.num_uv_components else 2
        uv = np.array(m.texcoords[0], np.float64).reshape(nv, ncomp)[:, :2]
        uv[:, 1] = 1.0 - uv[:, 1]                     # FBX v=0 is the bottom row; PVR row 0 is the top
        idx = np.array(m.indices, np.int64).reshape(-1, 3)
        pos_w = (np.c_[pos, np.ones(nv)] @ M.T)[:, :3]
        nrm_w = nrm @ np.linalg.inv(M[:3, :3])       # inverse-transpose for normals
        nrm_w /= np.maximum(np.linalg.norm(nrm_w, axis=1, keepdims=True), 1e-12)
        infl = [[] for _ in range(nv)]
        for b in (m.bones or []):
            if not b.weights:
                continue
            for vid, w in zip(b.weight_vertex_ids, b.weights):
                if w > 1e-4:
                    infl[vid].append((w, bone_index[b.name]))
        if not m.bones:                                # unskinned part (e.g. a helmet under `head`):
            n = owner                                  # rigid to the nearest bone ancestor
            while n is not None and n not in bone_index:
                n = nodes[n]["parent"]
            owner_bone = bone_index.get(n, 0)
            infl = [[(1.0, owner_bone)] for _ in range(nv)]
            print(f"  part {m.name} is unskinned: attached to bone {n if n in bone_index else '(root)'}")
        wb = np.zeros((nv, 2), np.int64)
        ww = np.zeros((nv, 2))
        for v, lst in enumerate(infl):
            lst.sort(reverse=True)
            lst = lst[:2] if lst else [(1.0, 0)]
            for k, (w, b) in enumerate(lst):
                wb[v, k], ww[v, k] = b, w
            if len(lst) == 1:
                wb[v, 1] = wb[v, 0]
        mname = scene.materials[m.material_index].get("NAME", "mat")
        if mname not in mats:
            mats.append(mname)
        P.append(pos_w); N.append(nrm_w); UV.append(uv); WB.append(wb); WW.append(ww)
        VMAT.append(np.full(nv, mats.index(mname), np.int64))
        TRI.append(idx + base)
        PART += [m.name] * len(idx)
        base += nv
        print(f"  part {m.name:24s} verts {nv:5d} tris {len(idx):5d}  material {mname}")
    mesh = dict(pos=np.concatenate(P), nrm=np.concatenate(N), uv=np.concatenate(UV),
                wb=np.concatenate(WB), ww=np.concatenate(WW), tris=np.concatenate(TRI), part=np.array(PART),
                vmat=np.concatenate(VMAT), mats=mats)
    return mesh, bones, order


# ============================================================================ 2. weights
def normalize_weights(mesh, rigid_below):
    ww, wb = mesh["ww"], mesh["wb"]
    s = ww.sum(1, keepdims=True)
    ww = ww / np.maximum(s, 1e-12)
    weak = ww[:, 1] < rigid_below
    ww[weak] = (1.0, 0.0)
    wb[weak, 1] = wb[weak, 0]
    same = wb[:, 0] == wb[:, 1]
    ww[same] = (1.0, 0.0)
    mesh["ww"], mesh["wb"] = ww, wb
    print(f"  influences: {int((~same & ~weak).sum())} blended (2 bones), {int((same | weak).sum())} rigid")


# ============================================================================ pose evaluation
def pose_matrices(bones, keys=None):
    """Skinning matrices (b, 4, 4) = world(t) @ inv_bind for a key row (None = bind pose)."""
    nb = len(bones)
    world = [None] * nb
    skin = np.zeros((nb, 4, 4))
    for i, b in enumerate(bones):
        q, t = (b["rest_rot"], b["rest_pos"]) if keys is None else (keys[i]["rot"], keys[i]["pos"])
        local = trs(mat_from_quat(np.asarray(q, np.float64) / np.linalg.norm(q)), t)
        world[i] = local if b["parent"] < 0 else world[b["parent"]] @ local
        ib = np.eye(4)
        ib[:3, :] = np.asarray(b["inv_bind"], np.float64).reshape(3, 4)
        skin[i] = world[i] @ ib
    return skin, world


def skin_positions(pos, wb, ww, skin):
    ph = np.c_[pos, np.ones(len(pos))]
    p0 = np.einsum("vij,vj->vi", skin[wb[:, 0]], ph)[:, :3]
    p1 = np.einsum("vij,vj->vi", skin[wb[:, 1]], ph)[:, :3]
    return p0 * ww[:, :1] + p1 * ww[:, 1:2]


# ============================================================================ 3. hidden surfaces
def fibonacci_dirs(n):
    i = np.arange(n) + 0.5
    phi = np.arccos(1 - 2 * i / n)
    th = np.pi * (1 + 5 ** 0.5) * i
    return np.c_[np.cos(th) * np.sin(phi), np.cos(phi), np.sin(th) * np.sin(phi)]


def pitch_band_dirs(lo_deg, hi_deg, n_pitch=5, n_yaw=12):
    """View directions (towards the camera) at every yaw, pitch within [lo, hi] above the horizon."""
    out = []
    for p in np.radians(np.linspace(lo_deg, hi_deg, n_pitch)):
        for y in np.radians(np.arange(n_yaw) * 360.0 / n_yaw):
            out.append(np.array([np.sin(y) * np.cos(p), np.sin(p), np.cos(y) * np.cos(p)]))
    return np.array(out)


def visible_triangles(mesh, bones, poses, views=40, res=320, dirs=None):
    """Triangles seen from at least one of `views` directions for any of the given poses.

    Conservative for the intended use (third-person camera): a triangle is kept if any of its
    pixels wins the depth test in any orthographic view of any pose."""
    tris = mesh["tris"]
    seen = np.zeros(len(tris), bool)
    dirs = fibonacci_dirs(views) if dirs is None else dirs
    for keys in poses:
        skin, _ = pose_matrices(bones, keys)
        p = skin_positions(mesh["pos"], mesh["wb"], mesh["ww"], skin)
        c = (p.min(0) + p.max(0)) / 2
        r = np.linalg.norm(p - c, axis=1).max() * 1.05
        for d in dirs:
            view = raster.look_at(c + d * r * 3, c, up=(0, 1, 0) if abs(d[1]) < 0.95 else (0, 0, 1))
            v = (np.c_[p, np.ones(len(p))] @ view.T)[:, :3]
            sx = (v[:, 0] / r * 0.5 + 0.5) * res
            sy = (0.5 - v[:, 1] / r * 0.5) * res
            seen[raster.visible_ids(sx, sy, v[:, 2], tris, res, res)] = True   # view z: larger = nearer
    return seen


# ============================================================================ 4/5. simplify + strips
def _meshopt_lib():
    """Native meshoptimizer (v0.22 ABI, see meshoptimizer.h in the sdist). The Python wrapper of
    0.2.30a0 lacks argtypes for simplifyWithAttributes and has no stripify, so bind them here."""
    import meshoptimizer
    d = os.path.dirname(meshoptimizer.__file__)
    so = next(f for f in os.listdir(d) if f.startswith("_meshoptimizer") and f.endswith(".so"))
    lib = ctypes.CDLL(os.path.join(d, so))
    P, S, F, U = ctypes.c_void_p, ctypes.c_size_t, ctypes.c_float, ctypes.c_uint
    lib.meshopt_stripify.restype = S
    lib.meshopt_stripify.argtypes = [P, P, S, S, U]
    lib.meshopt_stripifyBound.restype = S
    lib.meshopt_stripifyBound.argtypes = [S]
    lib.meshopt_simplifyWithAttributes.restype = S
    lib.meshopt_simplifyWithAttributes.argtypes = [P, P, S, P, S, S, P, S, P, S, P, S, F, U, P]
    return lib


def simplify(mesh, target_tris, uv_weight, normal_weight):
    tris = mesh["tris"]
    if len(tris) <= target_tris:
        return
    lib = _meshopt_lib()
    pos = np.ascontiguousarray(mesh["pos"], np.float32)
    # attributes: normals and UVs (UV seams between colour swatches are kept by the seam logic)
    attr = np.ascontiguousarray(np.c_[mesh["nrm"], mesh["uv"]], np.float32)
    wts = np.array([normal_weight] * 3 + [uv_weight] * 2, np.float32)
    idx = np.ascontiguousarray(tris.ravel(), np.uint32)
    dst = np.zeros_like(idx)
    err = np.zeros(1, np.float32)
    n = lib.meshopt_simplifyWithAttributes(dst.ctypes.data, idx.ctypes.data, idx.size, pos.ctypes.data, len(pos), 12,
                                           attr.ctypes.data, attr.shape[1] * 4, wts.ctypes.data, len(wts), None,
                                           target_tris * 3, 1.0, 0, err.ctypes.data)
    mesh["tris"] = dst[:n].reshape(-1, 3).astype(np.int64)
    mesh["part"] = np.array(["?"] * len(mesh["tris"]))
    print(f"  simplified {len(tris)} -> {len(mesh['tris'])} tris (relative error {float(err[0]):.4f})")


def simplify_bounded(mesh, max_error, uv_weight, normal_weight, part_error=None):
    """Error-bounded simplification (absolute, model units), one meshoptimizer call per material
    so small separate parts (eyes) cannot be collapsed away by the other material's budget.
    part_error {part name: bound}: those parts get their own call with their own bound (a face's
    nose and mouth are a few hundredths of a unit deep; KayKit parts are separate meshes)."""
    part_error = part_error or {}
    lib = _meshopt_lib()
    pos = np.ascontiguousarray(mesh["pos"], np.float32)
    attr = np.ascontiguousarray(np.c_[mesh["nrm"], mesh["uv"]], np.float32)
    wts = np.array([normal_weight] * 3 + [uv_weight] * 2, np.float32)
    tm = mesh["vmat"][mesh["tris"][:, 0]]
    own = np.isin(mesh["part"], list(part_error))
    out, worst = [], 0.0
    for k in range(len(mesh["mats"])):
        jobs = [(mesh["mats"][k], (tm == k) & ~own, max_error)]
        jobs += [(f"{mesh['mats'][k]}/{p}", (tm == k) & (mesh["part"] == p), e) for p, e in part_error.items()]
        for label, sel, bound in jobs:
            T = mesh["tris"][sel]
            if len(T) == 0:
                continue
            idx = np.ascontiguousarray(T.ravel(), np.uint32)
            dst = np.zeros_like(idx)
            err = np.zeros(1, np.float32)
            n = lib.meshopt_simplifyWithAttributes(dst.ctypes.data, idx.ctypes.data, idx.size, pos.ctypes.data, len(pos), 12,
                                                   attr.ctypes.data, attr.shape[1] * 4, wts.ctypes.data, len(wts), None,
                                                   0, float(bound), 4, err.ctypes.data)   # 4 = ErrorAbsolute
            out.append(dst[:n].reshape(-1, 3).astype(np.int64))
            worst = max(worst, float(err[0]))
            print(f"  {label:10s} {len(T)} -> {n // 3} tris (error {float(err[0]):.4f} <= {bound})")
    before = len(mesh["tris"])
    mesh["tris"] = np.concatenate(out)
    mesh["part"] = np.array(["?"] * len(mesh["tris"]))
    print(f"  simplified {before} -> {len(mesh['tris'])} tris, worst absolute error {worst:.4f} model units")


def compact(mesh):
    used = np.unique(mesh["tris"])
    remap = -np.ones(len(mesh["pos"]), np.int64)
    remap[used] = np.arange(len(used))
    for k in ("pos", "nrm", "uv", "wb", "ww", "vmat"):
        mesh[k] = mesh[k][used]
    mesh["tris"] = remap[mesh["tris"]]


def build_strips_and_groups(mesh):
    """Sorts vertices by bone group, returns (vertices, groups, strips)."""
    import meshoptimizer as mo
    wb, ww = mesh["wb"], mesh["ww"]
    rigid = ww[:, 1] <= 0
    # canonical group key: rigid -> (b, 0xFF); blended -> (min, max) with w1 = weight of the max bone
    b0 = np.where(rigid, wb[:, 0], np.minimum(wb[:, 0], wb[:, 1]))
    b1 = np.where(rigid, D.RIGID, np.maximum(wb[:, 0], wb[:, 1]))
    w1 = np.where(rigid, 0.0, np.where(wb[:, 1] == b1, ww[:, 1], ww[:, 0]))
    q = np.clip(np.round(w1 * 255), 0, 255).astype(np.int64)
    # blended vertices whose weight quantises to 0/255 are rigid in practice
    to_b0 = (~rigid) & (q == 0)
    to_b1 = (~rigid) & (q == 255)
    b1 = np.where(to_b0, D.RIGID, b1)
    b0 = np.where(to_b1, np.maximum(wb[:, 0], wb[:, 1]), b0)
    b1 = np.where(to_b1, D.RIGID, b1)
    q = np.where(b1 == D.RIGID, 0, q)
    key = b0 * 256 + b1
    order = np.lexsort((np.arange(len(key)), key))
    remap = np.empty_like(order)
    remap[order] = np.arange(len(order))

    verts = np.zeros(len(order), D.VERTEX_DT)
    verts["pos"] = mesh["pos"][order]
    verts["uv"] = mesh["uv"][order]
    verts["n"] = np.clip(np.round(mesh["nrm"][order] * 127), -127, 127)
    verts["w1"] = q[order]
    vmat = mesh.get("vmat", np.zeros(len(mesh["pos"]), np.int64))[order]
    groups = []
    k_sorted = key[order]
    start = 0
    for i in range(1, len(order) + 1):
        if i == len(order) or k_sorted[i] != k_sorted[start]:
            groups.append((start, i - start, int(k_sorted[start] // 256), int(k_sorted[start] % 256), 0))
            start = i
    groups = np.array(groups, D.GROUP_DT)

    tris = remap[mesh["tris"]]
    # one strip list per material (vertices are never shared across materials: assimp splits them)
    tmat = vmat[tris[:, 0]]
    per_mat = []
    for k in range(len(mesh.get("mats", ["mat"]))):
        T = tris[tmat == k]
        per_mat.append(_stripify(T, len(order)) if len(T) else [])
    strips = [s for sl in per_mat for s in sl]
    return verts, groups, strips, tris, per_mat


def _stripify(tris, nverts):
    import meshoptimizer as mo
    idx = np.ascontiguousarray(tris.ravel(), np.uint32)
    opt = np.zeros_like(idx)
    mo.optimize_vertex_cache_strip(opt, idx, vertex_count=nverts)
    lib = _meshopt_lib()
    cap = lib.meshopt_stripifyBound(len(opt))
    out = np.zeros(cap, np.uint32)
    RESTART = 0xFFFFFFFF
    n = lib.meshopt_stripify(out.ctypes.data, opt.ctypes.data, len(opt), nverts, RESTART)
    strips, cur = [], []
    for v in out[:n]:
        if v == RESTART:
            if cur:
                strips.append(cur)
            cur = []
        else:
            cur.append(int(v))
    if cur:
        strips.append(cur)
    return [s for s in strips if len(s) >= 3]


def check_strips_match(strips, tris):
    """Unstripified triangles must be exactly the input set, same winding (rotation-invariant)."""
    def canon(t):
        a, b, c = t
        k = min(range(3), key=lambda i: t[i])
        return tuple(t[k:] + t[:k])
    got = []
    for s in strips:
        for k in range(len(s) - 2):
            a, b, c = s[k], s[k + 1], s[k + 2]
            if a == b or b == c or a == c:
                continue
            got.append(canon([a, b, c] if k % 2 == 0 else [b, a, c]))
    want = sorted(canon(list(map(int, t))) for t in tris)
    return sorted(got) == want


# ============================================================================ 6. texture
def convert_texture(path, size, fmt):
    im = Image.open(path).convert("RGBA")
    w, h = (size, size) if isinstance(size, int) else size
    im = im.resize((w, h), Image.BOX)
    rgba = np.array(im)
    tex = pvrtex.twiddle(pvrtex.pack(rgba, fmt))
    return tex.tobytes(), w, h, rgba


# ============================================================================ 7. animation retarget
def retarget_clip(anim_path, bones, names, rest_world, fps_out=None, mapping=KAYKIT_FROM_MIXAMO,
                  prefix="mixamorig:", arm_spread_deg=0.0):
    import ufbx
    keep = []                                          # the ufbx binding needs its list objects alive

    def L(x):
        keep.append(x)
        return [x[i] for i in range(len(x))]

    def M(m):
        return np.array([[m.c0.x, m.c1.x, m.c2.x, m.c3.x], [m.c0.y, m.c1.y, m.c2.y, m.c3.y],
                         [m.c0.z, m.c1.z, m.c2.z, m.c3.z], [0, 0, 0, 1]], np.float64)

    scene = ufbx.load_file(anim_path)
    keep.append(scene)
    snodes = L(scene.nodes)
    snames = [n.name for n in snodes]
    stack = L(scene.anim_stacks)[0]
    anim = stack.anim
    src_fps = scene.settings.frames_per_second
    dur = stack.time_end - stack.time_begin
    frames = int(round(dur * src_fps))                 # loop period in source frames (last == first)
    fps = fps_out or src_fps
    n_out = int(round(frames / src_fps * fps))

    def src_world(t):
        es = ufbx.evaluate_scene(scene, anim, t)
        keep.append(es)
        en = es.nodes
        keep.append(en)
        return {snames[i]: M(en[i].node_to_world) for i in range(len(snames))}

    rest = {snames[i]: M(snodes[i].node_to_world) for i in range(len(snames))}
    for tgt, src in mapping.items():
        if tgt in names and prefix + src not in rest:
            sys.exit(f"error: source bone {prefix + src} missing in {anim_path}")
    # hips translation scale: target leg length / source leg length (rest, vertical)
    src_leg = rest[prefix + "LeftUpLeg"][1, 3] - rest[prefix + "LeftFoot"][1, 3]
    tgt_leg = rest_world[names.index("upperleg.l")][1, 3] - rest_world[names.index("foot.l")][1, 3]
    scale = tgt_leg / src_leg
    hips_src = prefix + mapping["hips"]

    samples = [src_world(stack.time_begin + k / fps) for k in range(n_out + 1)]
    hp = np.array([s[hips_src][:3, 3] for s in samples])
    drift = (hp[-1] - hp[0]) / n_out                  # root motion per output frame
    drift[1] = 0.0
    root_speed = float(np.linalg.norm(drift[[0, 2]]) * fps * scale)

    spread = {}
    if arm_spread_deg:
        a = np.radians(arm_spread_deg)
        for side, sgn in (("l", 1), ("r", -1)):
            c, s = np.cos(a * sgn), np.sin(a * sgn)
            spread["upperarm." + side] = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])   # about +Z (forward)

    nb = len(bones)
    keys = np.zeros((n_out, nb), D.KEY_DT)
    R_rest = [orthonormalize(rest_world[i]) for i in range(nb)]
    for k in range(n_out):
        S = samples[k]
        world_rot = [None] * nb
        for i in range(nb):
            name = names[i]
            par = int(bones[i]["parent"])
            rest_local_rot = mat_from_quat(bones[i]["rest_rot"])
            if name in mapping:
                sn = prefix + mapping[name]
                delta = orthonormalize(S[sn]) @ orthonormalize(rest[sn]).T
                if name in spread:
                    delta = delta @ spread[name]
                world_rot[i] = delta @ R_rest[i]
            else:
                world_rot[i] = (world_rot[par] if par >= 0 else np.eye(3)) @ rest_local_rot
            local_rot = (world_rot[par].T if par >= 0 else np.eye(3)) @ world_rot[i]
            q = quat_from_mat(local_rot)
            if k > 0 and np.dot(q, keys[k - 1, i]["rot"]) < 0:
                q = -q                                  # keep consecutive keys in one hemisphere
            pos = np.array(bones[i]["rest_pos"], np.float64)
            if name == "hips":
                d = (hp[k] - hp[0] - drift * k) * scale    # in-place: keep bob/sway, drop travel
                parent_rot = world_rot[par] if par >= 0 else np.eye(3)
                pos = pos + parent_rot.T @ d
            keys[k, i] = (q, pos)
    print(f"  clip from {os.path.basename(anim_path)}: {n_out} frames @ {fps:g} fps "
          f"({n_out / fps:.3f} s loop), leg scale {scale:.5f}, root speed {root_speed:.3f} units/s (stripped)")
    return keys, fps, root_speed


def ground_clip(mesh, bones, names, keys):
    """Per-frame foot planting: raise/lower the hips so the lowest skinned vertex sits on y = 0.

    Human walk cycles rotate heels/toes by angles sized for human feet; KayKit feet are much longer
    relative to the legs, so without this the toes/heels sink up to ~5% of the body height."""
    hips = names.index("hips")
    par = int(bones[hips]["parent"])
    before, after = [], []
    for k in range(keys.shape[0]):
        skin, world = pose_matrices(bones, keys[k])
        low = skin_positions(mesh["pos"], mesh["wb"], mesh["ww"], skin)[:, 1].min()
        before.append(low)
        parent_rot = world[par][:3, :3] if par >= 0 else np.eye(3)
        keys[k, hips]["pos"] = keys[k, hips]["pos"] + parent_rot.T @ np.array([0.0, -low, 0.0])
        skin, _ = pose_matrices(bones, keys[k])
        after.append(skin_positions(mesh["pos"], mesh["wb"], mesh["ww"], skin)[:, 1].min())
    print(f"  ground contact: lowest point per frame {min(before):+.3f}..{max(before):+.3f} "
          f"-> {min(after):+.4f}..{max(after):+.4f}")


_UFBX_KEEP = []        # ufbx objects kept alive for the whole run (see native_clip)
_UFBX_SCENES = {}      # one loaded scene per animation file


def native_clip(anim_path, stack_name, bones, names, fps_out=None):
    """A clip authored for this very rig (KayKit Rig_Medium animation packs): the keys are the
    bones' evaluated local transforms, no retargeting. The pack's T-pose equals the characters'
    bind pose and the clips loop (last frame == first), so one period is sampled without the
    duplicate end frame. Bones missing from the file keep their rest transform."""
    import ufbx
    keep = _UFBX_KEEP                      # never freed: the ufbx binding crashes on some GC orders

    def L(x):
        keep.append(x)
        return [x[i] for i in range(len(x))]

    if anim_path not in _UFBX_SCENES:          # load and list once per file (re-listing a scene's
        scene = ufbx.load_file(anim_path)       # nodes/stacks crashed the binding)
        keep.append(scene)
        snodes = L(scene.nodes)
        _UFBX_SCENES[anim_path] = (scene, snodes, [n.name for n in snodes], {a.name: a for a in L(scene.anim_stacks)})
    scene, snodes, snames, stacks = _UFBX_SCENES[anim_path]
    if stack_name not in stacks:
        sys.exit(f"error: {anim_path} has no clip {stack_name!r}; clips: {', '.join(stacks)}")
    st = stacks[stack_name]
    fps = fps_out or scene.settings.frames_per_second
    n_out = max(1, int(round((st.time_end - st.time_begin) * fps)))
    nb = len(bones)
    keys = np.zeros((n_out, nb), D.KEY_DT)
    idx = [snames.index(n) if n in snames else -1 for n in names]
    anim = st.anim
    keep.append(anim)
    for k in range(n_out):
        t = st.time_begin + k / fps
        for i in range(nb):
            if idx[i] < 0:
                keys[k, i] = (bones[i]["rest_rot"], bones[i]["rest_pos"])
                continue
            # per-node evaluation: evaluate_scene copies the whole scene per sample and crashed the
            # binding after ~130 samples; evaluate_transform gives the same local transform
            lt = ufbx.evaluate_transform(anim, snodes[idx[i]], t)
            q = np.array([lt.rotation.x, lt.rotation.y, lt.rotation.z, lt.rotation.w])
            if k > 0 and np.dot(q, keys[k - 1, i]["rot"]) < 0:
                q = -q
            keys[k, i] = (q / np.linalg.norm(q), (lt.translation.x, lt.translation.y, lt.translation.z))
    print(f"  native clip {stack_name} from {os.path.basename(anim_path)}: {n_out} frames @ {fps:g} fps "
          f"({n_out / fps:.3f} s loop)")
    return keys, fps


def axis_quat(axis, deg):
    a = np.radians(deg) * 0.5
    ax = np.asarray(axis, np.float64)
    return np.r_[ax * np.sin(a), np.cos(a)]


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])


def make_idle_clip(walk_keys, bones, names, frames=24, fps=12.0):
    """Standing idle derived from a walk cycle: the cycle's mean pose (legs average out to a
    stance, arms hang) plus a slow breathing loop (chest/head pitch, arm sway, hips dip).
    The KayKit rest pose is a T-pose, so it cannot serve as the idle pose itself."""
    nb = len(bones)
    mean = np.zeros(nb, D.KEY_DT)
    for b in range(nb):
        q = walk_keys[:, b]["rot"].astype(np.float64)
        q = q * np.where((q @ q[0]) < 0, -1.0, 1.0)[:, None]
        m = q.mean(0)
        mean[b] = (m / np.linalg.norm(m), walk_keys[:, b]["pos"].mean(0))
    height = 1.0
    if "head" in names:
        height = max(1e-3, np.linalg.inv(np.r_[np.asarray(bones[names.index("head")]["inv_bind"]).reshape(3, 4),
                                              [[0, 0, 0, 1]]])[1, 3])
    wobble = {"chest": ((1, 0, 0), 1.6), "head": ((1, 0, 0), -1.0), "spine": ((1, 0, 0), 0.6),
              "upperarm.l": ((0, 0, 1), 2.0), "upperarm.r": ((0, 0, 1), -2.0)}
    keys = np.zeros((frames, nb), D.KEY_DT)
    for k in range(frames):
        ph = 2 * np.pi * k / frames
        for b in range(nb):
            q, pos = mean[b]["rot"].astype(np.float64), mean[b]["pos"].astype(np.float64).copy()
            if names[b] in wobble:
                axis, deg = wobble[names[b]]
                q = quat_mul(q, axis_quat(axis, deg * np.sin(ph)))
            if names[b] == "hips":
                pos[1] -= 0.004 * height * (0.5 - 0.5 * np.cos(ph))
            keys[k, b] = (q / np.linalg.norm(q), pos)
    return keys, fps


# ============================================================================ main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True)
    ap.add_argument("--texture", required=True)
    ap.add_argument("--anim", action="append", default=[], help="FILE.fbx:clipname (Mixamo, retargeted; repeatable)")
    ap.add_argument("--native-anim", action="append", default=[],
                    help="FILE.fbx:STACK:clipname[:fps] - a clip authored for this rig (KayKit packs), "
                         "optionally resampled (the runtime interpolates); repeatable")
    ap.add_argument("--unlit", action="append", default=[],
                    help="MATERIAL=RRGGBB: draw that material untextured and unlit (e.g. glowing eyes)")
    ap.add_argument("--target-tris", type=int, default=2000)
    ap.add_argument("--max-error", type=float, default=None,
                    help="error-bounded simplification instead of --target-tris: absolute bound in model "
                         "units, per material (choose it with tools/dmf/qc_chars.py)")
    ap.add_argument("--tex-size", type=int, default=128)
    ap.add_argument("--tex-format", choices=["rgb565", "argb4444", "argb1555"], default="rgb565")
    ap.add_argument("--rigid-below", type=float, default=0.10, help="2nd influence below this -> rigid")
    ap.add_argument("--no-hidden-pass", action="store_true")
    ap.add_argument("--part-error", action="append", default=[],
                    help="PART=E: simplify that mesh part (e.g. Mage_Head) to its own error bound (with --max-error)")
    ap.add_argument("--view-pitch", default=None,
                    help="LO:HI degrees: the hidden-surface pass only considers views from this pitch band "
                         "(all yaws) and only the clips' poses (not the bind pose). Experimental, not used by "
                         "build_characters.sh: ~5 %% fewer triangles, worse QC (docs/SLICE_3D_NPCS.md)")
    ap.add_argument("--uv-weight", type=float, default=1.0)
    ap.add_argument("--normal-weight", type=float, default=0.25)
    ap.add_argument("--arm-spread", type=float, default=0.0, help="deg added to the retargeted upper-arm abduction")
    ap.add_argument("--double-sided", action="store_true")
    ap.add_argument("--no-ground", action="store_true", help="skip per-frame foot planting")
    ap.add_argument("--no-idle", action="store_true", help="do not derive an 'idle' clip from the first clip")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()

    print(f"[1] load {a.model}")
    mesh, bones, names = load_character(a.model)
    print(f"  {len(mesh['pos'])} verts, {len(mesh['tris'])} tris, {len(bones)} bones")
    rest_world = []
    for b in bones:
        ib = np.eye(4)
        ib[:3, :] = np.asarray(b["inv_bind"], np.float64).reshape(3, 4)
        rest_world.append(np.linalg.inv(ib))

    # bind pose sanity: FK of the rest locals must reproduce the bind pose (skin = identity)
    skin, _ = pose_matrices(bones)
    err = np.abs(skin - np.eye(4)).max()
    print(f"  bind-pose FK check: max |skin - I| = {err:.2e}")
    assert err < 1e-3, "rest locals do not reproduce the bind pose"

    print("[2] weights")
    normalize_weights(mesh, a.rigid_below)

    clips = []
    for spec in a.anim:
        path, _, name = spec.partition(":")
        print(f"[7] retarget {path}")
        keys, fps, speed = retarget_clip(path, bones, names, rest_world, arm_spread_deg=a.arm_spread)
        if not a.no_ground:
            ground_clip(mesh, bones, names, keys)
        clips.append(dict(name=name or "clip", fps=fps, flags=D.CLIP_LOOP, root_speed=speed, keys=keys))
    for spec in a.native_anim:
        path, stack, name, fps_s = (spec.split(":") + [None, None, None])[:4]
        print(f"[7] native clip {stack} ({path})")
        keys, fps = native_clip(path, stack, bones, names, float(fps_s) if fps_s else None)
        clips.append(dict(name=name or stack, fps=fps, flags=D.CLIP_LOOP, root_speed=0.0, keys=keys))
    if clips and not a.no_idle and not any(c["name"] == "idle" for c in clips):
        keys, fps = make_idle_clip(clips[0]["keys"], bones, names)
        if not a.no_ground:
            ground_clip(mesh, bones, names, keys)
        clips.append(dict(name="idle", fps=fps, flags=D.CLIP_LOOP, root_speed=0.0, keys=keys))
        print(f"  idle clip: {keys.shape[0]} frames @ {fps:g} fps, mean pose of '{clips[0]['name']}' + breathing")

    if not a.no_hidden_pass:
        print("[3] hidden-surface pass")
        if a.view_pitch:
            # field camera band: 12 yaws x 5 pitches LO..HI; the clips' poses only (every 4th key) -
            # the bind T-pose never appears in play. Crossfades stay within these poses' envelope.
            lo_p, hi_p = (float(v) for v in a.view_pitch.split(":"))
            poses = [c["keys"][k] for c in clips for k in range(0, len(c["keys"]), 4)]
            seen = visible_triangles(mesh, bones, poses, dirs=pitch_band_dirs(lo_p, hi_p))
        else:
            # bind pose + every 4th frame of each clip (every 24th of idles); long clips are spread to
            # about 6 poses so a 128-frame idle does not take 30 passes
            poses = [None] + [c["keys"][k] for c in clips
                              for k in range(0, len(c["keys"]), 24 if c["name"] == "idle" else max(4, len(c["keys"]) // 6))]
            seen = visible_triangles(mesh, bones, poses)
        removed = np.unique(mesh["part"][~seen], return_counts=True)
        print(f"  {int((~seen).sum())} of {len(seen)} tris never visible -> removed "
              f"({', '.join(f'{p}: {c}' for p, c in zip(*removed))})")
        mesh["tris"], mesh["part"] = mesh["tris"][seen], mesh["part"][seen]
        compact(mesh)

    if a.max_error is not None:
        print(f"[4] simplify, error <= {a.max_error} model units per material")
        part_error = {}
        for spec in a.part_error:
            p, _, e = spec.partition("=")
            if p not in set(mesh["part"]):
                sys.exit(f"--part-error: no part named {p!r} (parts: {', '.join(sorted(set(mesh['part'])))})")
            part_error[p] = float(e)
        simplify_bounded(mesh, a.max_error, a.uv_weight, a.normal_weight, part_error)
    else:
        print(f"[4] simplify to {a.target_tris} tris")
        simplify(mesh, a.target_tris, a.uv_weight, a.normal_weight)
    compact(mesh)

    print("[5] groups + strips")
    verts, groups, strips, tris_new, per_mat = build_strips_and_groups(mesh)
    ok = check_strips_match(strips, tris_new)
    n_idx = sum(len(s) for s in strips)
    print(f"  {len(verts)} verts in {len(groups)} bone groups "
          f"({int((groups['bone1'] == D.RIGID).sum())} rigid), {len(strips)} strips, {n_idx} strip indices "
          f"({n_idx / len(mesh['tris']):.2f} per tri), strip check {'OK' if ok else 'FAILED'}")
    assert ok

    print(f"[6] texture {a.texture} -> {a.tex_size}x{a.tex_size} {a.tex_format}")
    fmt = {"rgb565": pvrtex.FMT_RGB565, "argb4444": pvrtex.FMT_ARGB4444, "argb1555": pvrtex.FMT_ARGB1555}[a.tex_format]
    tex, tw, th, _ = convert_texture(a.texture, a.tex_size, fmt)

    m = D.Model()
    m.flags = D.FLAG_SKINNED | (D.FLAG_DOUBLE_SIDED if a.double_sided else 0)
    m.vertices, m.groups, m.strips, m.bones, m.clips = verts, groups, strips, bones, clips
    unlit = {}
    for spec in a.unlit:
        mat, _, rgb = spec.partition("=")
        unlit[mat] = 0xFF000000 | int(rgb, 16)
    if len(mesh["mats"]) == 1 and not unlit:
        m.tex_format, m.tex_w, m.tex_h, m.tex_data = fmt, tw, th, tex          # v1: one embedded texture
    else:
        # v2: one submesh per material; textured ones share the embedded texture, --unlit ones are flat
        m.textures = [dict(name="embedded", format=fmt, w=tw, h=th, flags=0, data=tex)]
        for k, mat in enumerate(mesh["mats"]):
            if not per_mat[k]:
                continue
            if mat in unlit:
                m.submeshes.append((per_mat[k], -1, D.SUB_UNLIT, unlit[mat]))
            else:
                m.submeshes.append((per_mat[k], 0, D.SUB_BILINEAR, 0xFFFFFFFF))
        m.strips = [s for sm in m.submeshes for s in sm[0]]
        print(f"  v2: {len(m.submeshes)} submeshes ({', '.join(mesh['mats'])})")
    m.finalize_bounds()
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    size = D.write(m, a.out)
    D.read(a.out)                                       # round-trip validation
    anim_bytes = sum(c["keys"].nbytes for c in clips)
    print(f"wrote {a.out}: {size} bytes (verts {verts.nbytes}, strips {n_idx * 2 + len(strips) * 2}, "
          f"bones {bones.nbytes}, anim {anim_bytes}, texture {len(tex)} = VRAM)")
    print(f"  tris {m.triangle_count()}  bbox {np.round(m.bbox, 3).tolist()}")


if __name__ == "__main__":
    main()
