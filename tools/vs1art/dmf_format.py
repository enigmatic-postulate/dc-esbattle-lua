"""VENDORED from the VS1 lineage (larp3-2/tools/dmf/dmf_format.py), unchanged below this note: the reader
for *its* DMF variant ("DMF1" v1/v2, 24-byte vertices, pre-twiddled textures). Our runtime format is
different (docs/DMF.md, tools/battle3d/dmfio.py); tools/vs1art/import_*.py convert between them.
"""
"""DMF (Dreamcast Mesh Format) v1/v2 - writer and reader.

Authoritative spec: docs/DMF.md. The C++ loader is src/dmf.h / src/dmf.cpp; both sides
static-assert the same record sizes. All values are little-endian (SH4 under KOS runs
little-endian), every section starts on a 32-byte boundary, offsets are from file start.

Layout
  Header            128 B
  Vertex[n]          24 B   float pos[3] | float uv[2] | int8 normal[3] (x127) | uint8 w1 (0..255)
  Group[g]            8 B   u16 first, u16 count, u8 bone0, u8 bone1 (0xFF = rigid), u16 pad
  u16 strip_len[s]
  u16 index[i]              concatenated strips; strip k has strip_len[k] indices (>= 3)
  Bone[b]            96 B   char name[16] | i16 parent | u16 pad | float inv_bind[12] (3x4 row-major)
                            | float rest_rot[4] (xyzw) | float rest_pos[3]
  Clip[c]            32 B   char name[16] | u16 frames | u16 flags | float fps | float root_speed | u32 data_off
  clip data                 frames x bones x (float rot[4] xyzw, float pos[3]) = 28 B per bone-frame
  texture                   PVR texels, already twiddled (tex_w * tex_h * 2 bytes)

v2 (slice 3D-2) adds, via the first 16 reserved header bytes (u16 submesh_count, u16 texture_count,
u16 kdop_count, u16 pad, u32 off_submeshes, u32 off_textures) plus u32 off_kdop right after:
  Submesh[s]         16 B   u16 first_strip, u16 strip_count, i16 texture (-1 = none), u16 flags
                            (1 BLEND: TR list, texture alpha; 2 BILINEAR; 4 UNLIT), u32 argb (base colour)
  TexRef[t]          32 B   char name[16], u16 format, u16 w, u16 h, u16 flags, u32 offset, u32 bytes
                            offset 0 = external: /rd/textures/<name>.dtx (shared, cached at runtime)
  KDop[k]            16 B   f32 n[3], f32 d   occluder polytope: the model lies in n.p <= d for all k
Model flag 16 PRELIT: the vertex's n[3]/w1 bytes hold baked R, G, B and 255 (static geometry);
v1 files (one texture, all strips) still load.
DTX texture file: 32-byte header ("DTX1", u16 format, u16 w, u16 h, u16 flags, u32 bytes, 16 pad)
followed by the twiddled texels.
"""
import struct
import numpy as np

MAGIC = b"DMF1"
VERSION = 1

FLAG_SKINNED = 1 << 0
FLAG_TEXTURED = 1 << 1
FLAG_ANIMATED = 1 << 2
FLAG_DOUBLE_SIDED = 1 << 3     # draw without back-face culling
FLAG_PRELIT = 1 << 4           # vertex n/w1 bytes carry baked RGB lighting (static props)

SUB_BLEND = 1 << 0             # submesh goes to the TR list (texture alpha)
SUB_BILINEAR = 1 << 1
SUB_UNLIT = 1 << 2             # ignore lighting: the submesh's argb is the final colour (glowing eyes)

CLIP_LOOP = 1 << 0

RIGID = 0xFF

HEADER_FMT = "<4sHHI" "HHHH" "HH" "I" "HH" "6f" "I" "f" "8I" "HHHH" "II" "I" "12x"
VERTEX_DT = np.dtype([("pos", "<f4", 3), ("uv", "<f4", 2), ("n", "i1", 3), ("w1", "u1")])
GROUP_DT = np.dtype([("first", "<u2"), ("count", "<u2"), ("bone0", "u1"), ("bone1", "u1"), ("pad", "<u2")])
BONE_DT = np.dtype([("name", "S16"), ("parent", "<i2"), ("pad", "<u2"), ("inv_bind", "<f4", 12),
                    ("rest_rot", "<f4", 4), ("rest_pos", "<f4", 3)])
CLIP_DT = np.dtype([("name", "S16"), ("frames", "<u2"), ("flags", "<u2"), ("fps", "<f4"),
                    ("root_speed", "<f4"), ("data_off", "<u4")])
KEY_DT = np.dtype([("rot", "<f4", 4), ("pos", "<f4", 3)])
SUBMESH_DT = np.dtype([("first_strip", "<u2"), ("strip_count", "<u2"), ("texture", "<i2"), ("flags", "<u2"),
                       ("argb", "<u4"), ("pad", "<u4")])
TEXREF_DT = np.dtype([("name", "S16"), ("format", "<u2"), ("w", "<u2"), ("h", "<u2"), ("flags", "<u2"),
                      ("offset", "<u4"), ("bytes", "<u4")])
KDOP_DT = np.dtype([("n", "<f4", 3), ("d", "<f4")])
DTX_FMT = "<4sHHHHI16x"

assert struct.calcsize(HEADER_FMT) == 128
assert VERTEX_DT.itemsize == 24 and GROUP_DT.itemsize == 8 and BONE_DT.itemsize == 96
assert CLIP_DT.itemsize == 32 and KEY_DT.itemsize == 28
assert SUBMESH_DT.itemsize == 16 and TEXREF_DT.itemsize == 32 and KDOP_DT.itemsize == 16
assert struct.calcsize(DTX_FMT) == 32

MAX_VERTICES = 65535
MAX_BONES = 32          # runtime pose buffers are fixed-size (src/dmf.h DMF_MAX_BONES)


class Model:
    """In-memory DMF model. Arrays use the dtypes above."""

    def __init__(self):
        self.flags = 0
        self.vertices = np.zeros(0, VERTEX_DT)
        self.groups = np.zeros(0, GROUP_DT)
        self.strips = []                       # list of index lists
        self.bones = np.zeros(0, BONE_DT)
        self.clips = []                        # list of dict(name, fps, flags, root_speed, keys[frames, bones] KEY_DT)
        self.tex_format = 0
        self.tex_w = self.tex_h = 0
        self.tex_data = b""                    # twiddled texels
        self.base_color = 0xFFFFFFFF
        self.bbox = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
        self.radius = 0.0
        # v2: submeshes [(strip list, texture index or -1, flags, argb)], textures [dict(name, format,
        # w, h, flags, data=b"" for external)], k-DOP [(n, d)]. Empty submeshes -> written as v1.
        self.submeshes = []
        self.textures = []
        self.kdop = []

    # ------------------------------------------------------------------ derived
    def triangle_count(self):
        return sum(len(s) - 2 for s in self.strips)

    def triangles(self):
        """Unstripified triangle list with the winding the PVR sees (odd triangles flipped)."""
        tris = []
        for s in self.strips:
            for k in range(len(s) - 2):
                a, b, c = s[k], s[k + 1], s[k + 2]
                tris.append((a, b, c) if k % 2 == 0 else (b, a, c))
        return np.array(tris, dtype=np.int64).reshape(-1, 3)

    def finalize_bounds(self):
        p = self.vertices["pos"]
        lo, hi = p.min(0), p.max(0)
        self.bbox = tuple(float(v) for v in (*lo, *hi))
        c = (lo + hi) * 0.5
        self.radius = float(np.sqrt(((p - c) ** 2).sum(1)).max())


def _align(buf, a=32):
    buf.extend(b"\0" * ((-len(buf)) % a))


def write(model, path):
    m = model
    v2 = bool(m.submeshes)
    strips = [s for sm in m.submeshes for s in sm[0]] if v2 else m.strips
    assert len(m.vertices) <= MAX_VERTICES and len(m.bones) <= MAX_BONES
    assert all(len(s) >= 3 for s in strips)
    body = bytearray(b"\0" * 128)
    off = {}

    def section(name, data):
        _align(body)
        off[name] = len(body)
        body.extend(data)

    section("vertices", m.vertices.tobytes())
    section("groups", m.groups.tobytes())
    section("strip_lens", np.array([len(s) for s in strips], "<u2").tobytes())
    idx = np.concatenate([np.asarray(s, "<u2") for s in strips]) if strips else np.zeros(0, "<u2")
    section("indices", idx.astype("<u2").tobytes())
    section("bones", m.bones.tobytes())
    # clip headers first, then key data (data_off patched in)
    clip_hdr = np.zeros(len(m.clips), CLIP_DT)
    _align(body)
    off["clips"] = len(body)
    body.extend(clip_hdr.tobytes())
    for i, c in enumerate(m.clips):
        _align(body)
        clip_hdr[i] = (c["name"].encode()[:15], c["keys"].shape[0], c["flags"], c["fps"], c["root_speed"], len(body))
        assert c["keys"].shape[1] == len(m.bones)
        body.extend(np.ascontiguousarray(c["keys"], KEY_DT).tobytes())
    body[off["clips"]:off["clips"] + clip_hdr.nbytes] = clip_hdr.tobytes()
    section("texture", m.tex_data)
    sub = np.zeros(len(m.submeshes), SUBMESH_DT)
    tex = np.zeros(len(m.textures), TEXREF_DT)
    kd = np.zeros(len(m.kdop), KDOP_DT)
    if v2:
        first = 0
        for i, (sl, t, fl, argb) in enumerate(m.submeshes):
            sub[i] = (first, len(sl), t, fl, argb, 0)
            first += len(sl)
        section("submeshes", sub.tobytes())
        tex_off = []
        for t in m.textures:
            if t.get("data"):
                section("tex_" + t["name"], t["data"])
                tex_off.append((off["tex_" + t["name"]], len(t["data"])))
            else:
                tex_off.append((0, 0))
        for i, t in enumerate(m.textures):
            tex[i] = (t["name"].encode()[:15], t["format"], t["w"], t["h"], t.get("flags", 0), *tex_off[i])
        section("textures", tex.tobytes())
        for i, (n, d) in enumerate(m.kdop):
            kd[i] = (n, d)
        section("kdop", kd.tobytes())
    _align(body)

    flags = m.flags
    if m.clips:
        flags |= FLAG_ANIMATED
    if m.tex_format or m.textures:
        flags |= FLAG_TEXTURED
    hdr = struct.pack(HEADER_FMT, MAGIC, 2 if v2 else 1, flags, len(body),
                      len(m.vertices), len(m.groups), len(m.bones), len(m.clips),
                      len(strips), m.tex_format, int(idx.size), m.tex_w, m.tex_h,
                      *m.bbox, m.base_color, m.radius,
                      off["vertices"], off["groups"], off["strip_lens"], off["indices"],
                      off["bones"], off["clips"], off["texture"], len(m.tex_data),
                      len(sub), len(tex), len(kd), 0, off.get("submeshes", 0), off.get("textures", 0),
                      off.get("kdop", 0))
    body[0:128] = hdr
    with open(path, "wb") as f:
        f.write(body)
    return len(body)


def write_dtx(path, fmt, w, h, texels, flags=0):
    data = struct.pack(DTX_FMT, b"DTX1", fmt, w, h, flags, len(texels)) + bytes(texels)
    with open(path, "wb") as f:
        f.write(data)
    return len(data)


def read_dtx(path):
    data = open(path, "rb").read()
    magic, fmt, w, h, flags, n = struct.unpack(DTX_FMT, data[:32])
    assert magic == b"DTX1" and n == w * h * 2 and len(data) == 32 + n, path
    return fmt, w, h, flags, data[32:]


def read(path):
    data = open(path, "rb").read()
    h = struct.unpack(HEADER_FMT, data[:128])
    (magic, version, flags, size, nv, ng, nb, nc, ns, tex_fmt, ni, tw, th,
     x0, y0, z0, x1, y1, z1, base_color, radius,
     o_v, o_g, o_sl, o_i, o_b, o_c, o_t, tex_bytes, n_sub, n_tex, n_kdop, _pad, o_sub, o_tex, o_kdop) = h
    assert magic == MAGIC and version in (1, 2), (magic, version)
    assert size == len(data), "file size mismatch"
    m = Model()
    m.flags = flags
    m.vertices = np.frombuffer(data, VERTEX_DT, nv, o_v).copy()
    m.groups = np.frombuffer(data, GROUP_DT, ng, o_g).copy()
    lens = np.frombuffer(data, "<u2", ns, o_sl)
    idx = np.frombuffer(data, "<u2", ni, o_i)
    assert lens.sum() == ni
    m.strips, p = [], 0
    for n in lens:
        m.strips.append(idx[p:p + n].astype(np.int64).tolist())
        p += n
    m.bones = np.frombuffer(data, BONE_DT, nb, o_b).copy()
    for c in np.frombuffer(data, CLIP_DT, nc, o_c):
        keys = np.frombuffer(data, KEY_DT, int(c["frames"]) * nb, int(c["data_off"])).reshape(int(c["frames"]), nb)
        m.clips.append(dict(name=c["name"].decode(), fps=float(c["fps"]), flags=int(c["flags"]),
                            root_speed=float(c["root_speed"]), keys=keys.copy()))
    m.tex_format, m.tex_w, m.tex_h = tex_fmt, tw, th
    m.tex_data = data[o_t:o_t + tex_bytes]
    m.base_color = base_color
    m.bbox = (x0, y0, z0, x1, y1, z1)
    m.radius = radius
    if version == 2:
        for t in np.frombuffer(data, TEXREF_DT, n_tex, o_tex):
            m.textures.append(dict(name=t["name"].decode(), format=int(t["format"]), w=int(t["w"]), h=int(t["h"]),
                                   flags=int(t["flags"]),
                                   data=data[int(t["offset"]):int(t["offset"]) + int(t["bytes"])] if t["offset"] else b""))
        for sm in np.frombuffer(data, SUBMESH_DT, n_sub, o_sub):
            a, n = int(sm["first_strip"]), int(sm["strip_count"])
            assert a + n <= ns, "submesh strip range"
            assert -1 <= sm["texture"] < n_tex, "submesh texture index"
            m.submeshes.append((m.strips[a:a + n], int(sm["texture"]), int(sm["flags"]), int(sm["argb"])))
        assert sum(len(sm[0]) for sm in m.submeshes) == ns, "submeshes must cover every strip"
        m.kdop = [(k["n"].copy(), float(k["d"])) for k in np.frombuffer(data, KDOP_DT, n_kdop, o_kdop)]
    # structural validation (same checks as the C++ loader)
    assert all(0 <= i < nv for s in m.strips for i in s), "index out of range"
    first = 0
    for g in m.groups:
        assert g["first"] == first, "groups must tile the vertex array in order"
        first += int(g["count"])
        assert g["bone0"] < max(nb, 1) and (g["bone1"] == RIGID or g["bone1"] < nb)
    assert first == nv, "groups must cover every vertex"
    for i, b in enumerate(m.bones):
        assert -1 <= b["parent"] < i, "bones must be ordered parents-first"
    return m
