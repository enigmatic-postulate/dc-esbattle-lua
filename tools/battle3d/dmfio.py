"""dmfio.py - read/write .dmf models (docs/DMF.md) and .dma animation libraries (docs/DMA.md),
plus the reference pose / skinning math the runtime (src/dmf.cpp, src/anim.cpp) must match.

Pure Python + numpy (no Blender): used by the build tools (which run in Blender's Python), the
render/QC tools and the tests.

Pose rule (runtime == reference):
    local = T(t) * R(q); world = world(parent) * local; skin = world * inv_bind
    p' = w0 * skin(b0) p + (1 - w0) * skin(b1) p
    clips are sampled by nlerp between frames (shortest path); looping clips wrap last -> first
"""
import struct
import numpy as np

# ---------------------------------------------------------------------------- DMF
HDR = struct.Struct('<4sHHI6HI3f3f8I12s')          # 96 bytes
VTX = np.dtype([('x', '<f4'), ('y', '<f4'), ('z', '<f4'), ('u', '<f4'), ('v', '<f4'),
                ('nx', 'i1'), ('ny', 'i1'), ('nz', 'i1'), ('w0', 'u1'), ('argb', '<u4'), ('pad', 'V4')])
GRP = np.dtype([('first', '<u2'), ('count', '<u2'), ('bone0', 'u1'), ('bone1', 'u1'), ('pad', '<u2')])
BAT = struct.Struct('<IIHHI')
BONE = struct.Struct('<16shH3f4f12f')
ANIM = struct.Struct('<16sHBBffI')
TEXH = struct.Struct('<4HI20s')
MAT = struct.Struct('<48sHH12s')
assert HDR.size == 96 and VTX.itemsize == 32 and GRP.itemsize == 8 and BAT.size == 16
assert BONE.size == 96 and ANIM.size == 32 and TEXH.size == 32 and MAT.size == 64

SKINNED, TEXTURED = 1, 2
BATCH_DOUBLE_SIDED, BATCH_TRANSLUCENT = 1, 2
MAT_BILINEAR, MAT_ALPHA = 1, 2
TEX_RGB565, TEX_ARGB4444, TEX_ARGB1555 = 1, 2, 3
NO_BONE = 0xFF


def _cstr(b):
    return b.split(b'\0', 1)[0].decode('ascii')


def read_dmf(path_or_bytes):
    b = open(path_or_bytes, 'rb').read() if isinstance(path_or_bytes, str) else bytes(path_or_bytes)
    h = HDR.unpack_from(b, 0)
    magic, ver, flags, size, nv, ng, nb, na, nbat, nmat, nsw = h[:11]
    bmin, bmax = h[11:14], h[14:17]
    off_v, off_g, off_s, off_b, off_bones, off_a, off_t, off_m = h[17:25]
    assert magic == b'DMF1' and size == len(b), 'not a DMF image'
    m = dict(version=ver, flags=flags, bmin=np.array(bmin), bmax=np.array(bmax))
    m['verts'] = np.frombuffer(b, VTX, nv, off_v).copy()
    m['groups'] = np.frombuffer(b, GRP, ng, off_g).copy()
    m['strip_words'] = np.frombuffer(b, '<u2', nsw, off_s).copy()
    m['batches'] = [dict(zip(('first_word', 'num_words', 'num_strips', 'flags', 'num_tris'), BAT.unpack_from(b, off_b + 16 * k)))
                    for k in range(nbat)]
    bones = []
    for i in range(nb):
        r = BONE.unpack_from(b, off_bones + 96 * i)
        bones.append(dict(name=_cstr(r[0]), parent=r[1], flags=r[2], t=np.array(r[3:6]), q=np.array(r[6:10]),
                          inv_bind=np.array(r[10:22]).reshape(3, 4)))
    m['bones'] = bones
    anims = []
    for k in range(na):
        name, nf, fl, tb, fps, gs, od = ANIM.unpack_from(b, off_a + 32 * k)
        stride = 12 + 8 * nb
        t = np.zeros((nf, 3)); q = np.zeros((nf, nb, 4))
        for f in range(nf):
            o = od + stride * f
            t[f] = struct.unpack_from('<3f', b, o)
            q[f] = np.frombuffer(b, '<i2', nb * 4, o + 12).reshape(nb, 4) / 32767.0
        anims.append(dict(name=_cstr(name), frames=nf, loop=bool(fl & 1), t_bone=tb, fps=fps, ground_speed=gs, t=t, q=q))
    m['anims'] = anims
    mats = []
    for k in range(nmat):
        tex, fl, _, _ = MAT.unpack_from(b, off_m + 64 * k)
        mats.append(dict(tex=_cstr(tex), flags=fl))
    m['materials'] = mats
    m['texture'] = None
    if flags & TEXTURED:
        w, hh, fmt, filt, bc, _ = TEXH.unpack_from(b, off_t)
        m['texture'] = dict(w=w, h=hh, format=fmt, filter=filt,
                            texels=np.frombuffer(b, '<u2', w * hh, off_t + 32).reshape(hh, w).copy())
    return m


def strips_of_batch(m, k):
    """List of index lists (one per strip) of batch k."""
    bt = m['batches'][k]
    w = m['strip_words'][bt['first_word']:bt['first_word'] + bt['num_words']]
    out, i = [], 0
    while i < len(w):
        n = int(w[i]); out.append([int(x) for x in w[i + 1:i + 1 + n]]); i += 1 + n
    return out


def strip_triangles(strip):
    """Triangles of one strip, with the standard parity (odd triangles swapped), CCW kept."""
    tris = []
    for j in range(len(strip) - 2):
        a, b_, c = strip[j], strip[j + 1], strip[j + 2]
        if j & 1:
            a, b_ = b_, a
        if a != b_ and b_ != c and a != c:
            tris.append((a, b_, c))
    return tris


def _align(n, a):
    return (n + a - 1) // a * a


def write_dmf(verts, groups, batches, strip_words, bones=(), anims=(), materials=(), texture=None, version=2):
    """verts: structured VTX array; groups: GRP array; batches: list of dicts (flags incl. material<<8);
    strip_words: u16 array; bones: dicts name,parent,t,q,inv_bind(3x4); anims: dicts (DMF v1 clips:
    name, loop, t_bone, fps, ground_speed, t (F,3), q (F,nb,4)); materials: dicts tex, flags;
    texture: dict w,h,format,filter,texels (h,w u16). Returns the file bytes."""
    nb = len(bones)
    flags = (SKINNED if nb else 0) | (TEXTURED if texture is not None else 0)
    # Section order and 32-byte alignment of every section: the layout of the original converter
    # (tools/dmf/dmf.py), so rebuilt files are byte-identical to it.
    off = 96
    off_v = off; off = _align(off + 32 * len(verts), 32)
    off_g = off; off = _align(off + 8 * len(groups), 32)
    off_s = off; off = _align(off + 2 * len(strip_words), 32)
    off_b = off; off = _align(off + 16 * len(batches), 32)
    off_m = 0
    if materials:
        off_m = off; off = _align(off + 64 * len(materials), 32)
    off_bones = 0
    if nb:
        off_bones = off; off = _align(off + 96 * nb, 32)
    off_a = 0
    anim_data_off = []
    if anims:
        off_a = off; off = _align(off + 32 * len(anims), 32)
        for a in anims:
            anim_data_off.append(off); off = _align(off + (12 + 8 * nb) * len(a['t']), 32)
    off_t = 0
    if texture is not None:
        off_t = off; off += 32 + 2 * texture['w'] * texture['h']
    size = _align(off, 32)
    b = bytearray(size)
    p = np.stack([verts['x'], verts['y'], verts['z']], 1)
    bmin, bmax = p.min(0), p.max(0)
    HDR.pack_into(b, 0, b'DMF1', version, flags, size, len(verts), len(groups), nb, len(anims), len(batches), len(materials),
                  len(strip_words), *bmin, *bmax, off_v, off_g, off_s, off_b, off_bones, off_a, off_t, off_m, b'\0' * 12)
    for k, mt in enumerate(materials):
        MAT.pack_into(b, off_m + 64 * k, mt['tex'].encode(), mt['flags'], 0, b'\0' * 12)
    b[off_v:off_v + 32 * len(verts)] = np.ascontiguousarray(verts, VTX).tobytes()
    b[off_g:off_g + 8 * len(groups)] = np.ascontiguousarray(groups, GRP).tobytes()
    b[off_s:off_s + 2 * len(strip_words)] = np.asarray(strip_words, '<u2').tobytes()
    for k, bt in enumerate(batches):
        BAT.pack_into(b, off_b + 16 * k, bt['first_word'], bt['num_words'], bt['num_strips'], bt['flags'], bt['num_tris'])
    for i, bn in enumerate(bones):
        BONE.pack_into(b, off_bones + 96 * i, bn['name'].encode(), bn['parent'], 0, *bn['t'], *bn['q'],
                       *np.asarray(bn['inv_bind'], float).reshape(12))
    for k, a in enumerate(anims):
        ANIM.pack_into(b, off_a + 32 * k, a['name'].encode(), len(a['t']), 1 if a['loop'] else 0, a['t_bone'], a['fps'],
                       a.get('ground_speed', 0.0), anim_data_off[k])
        for f in range(len(a['t'])):
            o = anim_data_off[k] + (12 + 8 * nb) * f
            struct.pack_into('<3f', b, o, *a['t'][f])
            b[o + 12:o + 12 + 8 * nb] = quantize_q(a['q'][f]).tobytes()
    if texture is not None:
        TEXH.pack_into(b, off_t, texture['w'], texture['h'], texture['format'], texture['filter'],
                       2 * texture['w'] * texture['h'], b'\0' * 20)
        b[off_t + 32:off_t + 32 + 2 * texture['w'] * texture['h']] = np.asarray(texture['texels'], '<u2').tobytes()
    return bytes(b)


def quantize_q(q):
    """(n,4) quaternions x,y,z,w -> int16 * 32767 (rounded), hemisphere kept as given."""
    return np.clip(np.round(np.asarray(q) * 32767.0), -32767, 32767).astype('<i2')


# ---------------------------------------------------------------------------- DMA (animation library)
# docs/DMA.md. Little-endian.
DMA_HDR = struct.Struct('<4sHHIHHII40s')             # 64 bytes
DMA_CLIP = struct.Struct('<20sHBBfI4H4fII')          # 64 bytes
assert DMA_HDR.size == 64 and DMA_CLIP.size == 64
LOOP = 1
EV_NONE = 0xFFFF
MAX_TBONES = 12


def write_dma(bone_names, clips):
    """clips: dicts name, loop, fps, t_bones (sorted bone indices), t (F, nt, 3), q (F, nb, 4),
    events (4 tick values or EV_NONE), bound (cx, cy, cz, r)."""
    nb = len(bone_names)
    off_names = 64
    off_clips = _align(off_names + 16 * nb, 32)
    off = off_clips + 64 * len(clips)
    data_off = []
    for c in clips:
        off = _align(off, 4)
        data_off.append(off)
        off += (12 * len(c['t_bones']) + 8 * nb) * len(c['q'])
    size = _align(off, 32)
    b = bytearray(size)
    DMA_HDR.pack_into(b, 0, b'DMA1', 1, nb, size, len(clips), 0, off_names, off_clips, b'\0' * 40)
    for i, n in enumerate(bone_names):
        b[off_names + 16 * i:off_names + 16 * i + 16] = n.encode().ljust(16, b'\0')[:16]
    for k, c in enumerate(clips):
        tb = sorted(c['t_bones'])
        assert len(tb) <= MAX_TBONES and len(c['name']) < 20
        mask = 0
        for i in tb:
            mask |= 1 << i
        stride = 12 * len(tb) + 8 * nb
        ev = list(c.get('events', [])) + [EV_NONE] * 4
        DMA_CLIP.pack_into(b, off_clips + 64 * k, c['name'].encode(), len(c['q']), LOOP if c['loop'] else 0, len(tb),
                           c['fps'], mask, *ev[:4], *c['bound'], data_off[k], stride)
        for f in range(len(c['q'])):
            o = data_off[k] + stride * f
            for j in range(len(tb)):
                struct.pack_into('<3f', b, o + 12 * j, *c['t'][f][j])
            b[o + 12 * len(tb):o + stride] = quantize_q(c['q'][f]).tobytes()
    return bytes(b)


def read_dma(path_or_bytes):
    b = open(path_or_bytes, 'rb').read() if isinstance(path_or_bytes, str) else bytes(path_or_bytes)
    magic, ver, nb, size, nc, _, off_names, off_clips, _ = DMA_HDR.unpack_from(b, 0)
    assert magic == b'DMA1' and size == len(b)
    names = [_cstr(b[off_names + 16 * i:off_names + 16 * i + 16]) for i in range(nb)]
    clips = []
    for k in range(nc):
        r = DMA_CLIP.unpack_from(b, off_clips + 64 * k)
        name, nf, fl, nt, fps, mask = _cstr(r[0]), r[1], r[2], r[3], r[4], r[5]
        ev, bound, od, stride = list(r[6:10]), list(r[10:14]), r[14], r[15]
        tb = [i for i in range(nb) if mask >> i & 1]
        t = np.zeros((nf, nt, 3)); q = np.zeros((nf, nb, 4))
        for f in range(nf):
            o = od + stride * f
            if nt:
                t[f] = np.frombuffer(b, '<f4', 3 * nt, o).reshape(nt, 3)
            q[f] = np.frombuffer(b, '<i2', 4 * nb, o + 12 * nt).reshape(nb, 4) / 32767.0
        clips.append(dict(name=name, frames=nf, loop=bool(fl & LOOP), fps=fps, t_bones=tb, t=t, q=q, events=ev, bound=bound))
    return dict(bones=names, clips=clips)


# ---------------------------------------------------------------------------- reference math
def quat_to_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def nlerp(a, b, w):
    if np.dot(a, b) < 0:
        b = -b
    r = a + (b - a) * w
    return r / np.linalg.norm(r)


def bind_pose(m):
    return [np.array(bn['q']) for bn in m['bones']], [np.array(bn['t']) for bn in m['bones']]


def sample_clip(m, clip, t_sec, remap=None):
    """Pose (q list, t list) of a DMA clip at t_sec on model m (bind translations for the others).
    remap[i] = library bone index of model bone i (identity if None)."""
    nb = len(m['bones'])
    F = clip['frames']
    f = t_sec * clip['fps']
    if clip['loop']:
        f = f % F
        i0 = min(int(f), F - 1); i1 = 0 if i0 + 1 == F else i0 + 1
    else:
        f = min(max(f, 0.0), F - 1.0)
        i0 = int(f); i1 = min(i0 + 1, F - 1)
    w = f - i0
    qs, ts = bind_pose(m)
    tb = clip['t_bones']
    for i in range(nb):
        li = remap[i] if remap is not None else i
        if li < 0:
            continue
        qs[i] = nlerp(clip['q'][i0][li], clip['q'][i1][li], w)
        if li in tb:
            j = tb.index(li)
            ts[i] = clip['t'][i0][j] + (clip['t'][i1][j] - clip['t'][i0][j]) * w
    return qs, ts


def world_mats(m, qs, ts):
    out = []
    for i, bn in enumerate(m['bones']):
        L = np.eye(4); L[:3, :3] = quat_to_mat(qs[i]); L[:3, 3] = ts[i]
        out.append(L if bn['parent'] < 0 else out[bn['parent']] @ L)
    return out


def skin_mats(m, qs, ts):
    W = world_mats(m, qs, ts)
    out = []
    for i, bn in enumerate(m['bones']):
        inv = np.eye(4); inv[:3, :] = bn['inv_bind']
        out.append(W[i] @ inv)
    return out


def skinned_positions(m, skin):
    """Model-space positions (nv, 3) and normals (nv, 3) of every vertex under skin matrices."""
    v = m['verts']
    p = np.stack([v['x'], v['y'], v['z'], np.ones(len(v))], 1)
    n = np.stack([v['nx'], v['ny'], v['nz']], 1).astype(float) / 127.0
    out_p = np.zeros((len(v), 3)); out_n = np.zeros((len(v), 3))
    if not m['bones']:
        return p[:, :3].copy(), n
    for g in m['groups']:
        s, e = int(g['first']), int(g['first']) + int(g['count'])
        A = skin[g['bone0']]
        pa = p[s:e] @ A.T; na = n[s:e] @ A[:3, :3].T
        if g['bone1'] != NO_BONE:
            B = skin[g['bone1']]
            w0 = (v['w0'][s:e].astype(float) / 255.0)[:, None]
            pb = p[s:e] @ B.T; nb_ = n[s:e] @ B[:3, :3].T
            pa = pa * w0 + pb * (1 - w0); na = na * w0 + nb_ * (1 - w0)
        out_p[s:e] = pa[:, :3]; out_n[s:e] = na
    ln = np.linalg.norm(out_n, axis=1, keepdims=True)
    return out_p, out_n / np.maximum(ln, 1e-9)


def texture_rgba(tex):
    """Embedded texture -> (h, w, 4) uint8 RGBA."""
    t = tex['texels'].astype(np.uint32)
    if tex['format'] == TEX_RGB565:
        r = (t >> 11) & 31; g = (t >> 5) & 63; b_ = t & 31
        return np.stack([r * 255 // 31, g * 255 // 63, b_ * 255 // 31, np.full_like(t, 255)], -1).astype(np.uint8)
    if tex['format'] == TEX_ARGB4444:
        a = (t >> 12) & 15; r = (t >> 8) & 15; g = (t >> 4) & 15; b_ = t & 15
        return (np.stack([r, g, b_, a], -1) * 17).astype(np.uint8)
    a = (t >> 15) & 1; r = (t >> 10) & 31; g = (t >> 5) & 31; b_ = t & 31
    return np.stack([r * 255 // 31, g * 255 // 31, b_ * 255 // 31, a * 255], -1).astype(np.uint8)
