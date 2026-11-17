#!/usr/bin/env python3
"""import_chars.py - a VS1 character (assets/vs1_art/models/<name>.dmf) into our DMF (docs/DMF.md).

VS1 built its characters from the same KayKit FBX files as ours, with a finer error bound (hero 2,272 triangles
vs our 1,490) and 128x128 textures (ours 64x64). This converts one, losslessly where the formats allow:

  * bones: VS1 keeps the FBX armature node 'Rig_Medium' (identity) as bone 0; it is dropped, the rest is the
    same Rig_Medium skeleton as ours, in the same order (checked), so the animation library binds unchanged
  * vertices: position, UV, normal; VS1 stores the weight of bone1, ours the weight of bone0 (w0 = 255 - w1)
  * groups: bone indices shifted by the dropped root
  * texture: untwiddled (ours is linear; the loader twiddles), same RGB565 texels, bilinear
  * v2 submeshes: the textured one -> material '@'; an UNLIT untextured one (the skeleton's glowing eyes)
    -> material '' whose vertices carry the glow colour (lit like the rest: our runtime has no unlit flag)
  * clips: VS1's own idle/walk are NOT taken; --clips-from copies the embedded clips of one of our models
    (same skeleton), so the field's locomotion (stride-matched walk/run, field3d.cpp) is unchanged

    python3 tools/vs1art/import_chars.py hero out.dmf --clips-from romdisk/models/knight.dmf
"""
import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
import dmf_format as vs1  # noqa: E402
import pvrtex  # noqa: E402
import dmfio  # noqa: E402

SRC = os.path.join(ROOT, 'assets', 'vs1_art', 'models')


def convert(name, clips_from=None):
    m = vs1.read(os.path.join(SRC, name + '.dmf'))
    names = [b['name'].decode() for b in m.bones]
    drop = 1 if names and names[0] == 'Rig_Medium' else 0
    if drop:
        b0 = m.bones[0]
        assert np.allclose(b0['inv_bind'], [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]) and np.allclose(b0['rest_rot'], [0, 0, 0, 1]) \
            and np.allclose(b0['rest_pos'], 0), 'Rig_Medium is not identity'
        assert not any(g['bone0'] == 0 or (g['bone1'] == 0) for g in m.groups), 'vertices on Rig_Medium'
    bones = []
    for b in m.bones[drop:]:
        bones.append(dict(name=b['name'].decode(), parent=int(b['parent']) - drop if b['parent'] >= drop else -1,
                          t=np.array(b['rest_pos'], float), q=np.array(b['rest_rot'], float),
                          inv_bind=np.array(b['inv_bind'], float).reshape(3, 4)))
    v = m.vertices
    vt = np.zeros(len(v), dmfio.VTX)
    vt['x'], vt['y'], vt['z'] = v['pos'][:, 0], v['pos'][:, 1], v['pos'][:, 2]
    vt['u'], vt['v'] = v['uv'][:, 0], v['uv'][:, 1]
    vt['nx'], vt['ny'], vt['nz'] = v['n'][:, 0], v['n'][:, 1], v['n'][:, 2]
    vt['w0'] = 255 - v['w1'].astype(np.int32)
    vt['argb'] = 0xFFFFFFFF
    groups = np.zeros(len(m.groups), dmfio.GRP)
    for i, g in enumerate(m.groups):
        b1 = int(g['bone1'])
        groups[i] = (g['first'], g['count'], int(g['bone0']) - drop, dmfio.NO_BONE if b1 == vs1.RIGID else b1 - drop, 0)
    # texture (v1: the model's own; v2: the embedded TexRef)
    if m.submeshes:
        subs = m.submeshes
        emb = [t for t in m.textures if t['data']]
        assert len(emb) == 1, 'one embedded texture expected'
        fmt, tw, th, data = emb[0]['format'], emb[0]['w'], emb[0]['h'], emb[0]['data']
    else:
        subs = [(m.strips, 0, vs1.SUB_BILINEAR, 0xFFFFFFFF)]
        fmt, tw, th, data = m.tex_format, m.tex_w, m.tex_h, m.tex_data
    assert fmt == pvrtex.FMT_RGB565
    texels = pvrtex.untwiddle(np.frombuffer(data, '<u2'), tw, th)
    texture = dict(w=tw, h=th, format=dmfio.TEX_RGB565, filter=1, texels=texels)
    materials, batches, words = [], [], []
    for strips, t, fl, argb in subs:
        if t >= 0:
            mat = dict(tex='@', flags=dmfio.MAT_BILINEAR)
        else:                                   # an unlit colour (the glowing eyes): vertex colour, untextured
            mat = dict(tex='', flags=0)
            for st in strips:
                vt['argb'][np.array(st)] = argb
        if mat not in materials:
            materials.append(mat)
        first = len(words)
        for st in strips:
            words.append(len(st))
            words.extend(st)
        batches.append(dict(first_word=first, num_words=len(words) - first, num_strips=len(strips),
                            flags=(materials.index(mat) << 8) | dmfio.BATCH_DOUBLE_SIDED, num_tris=sum(len(s) - 2 for s in strips)))
    anims = []
    if clips_from:
        src = dmfio.read_dmf(clips_from)
        assert [b['name'] for b in src['bones']] == [b['name'] for b in bones], 'clip source has another skeleton'
        for a, b in zip(src['bones'], bones):      # the same Rig_Medium bind pose (toes.r's rest quaternion is stored negated)
            assert a['parent'] == b['parent'] and np.abs(a['inv_bind'] - b['inv_bind']).max() < 1e-4, a['name']   # 0.1 mm: FBX loader float noise
        bones = src['bones']                      # the clips' own rest pose, bit for bit
        anims = src['anims']
    if materials == [dict(tex='@', flags=dmfio.MAT_BILINEAR)]:
        materials = []                          # v1-style: the embedded texture, no material table
    return dmfio.write_dmf(vt, groups, batches, np.array(words, '<u2'), bones, anims, materials, texture)


def vs1_texture(name):
    """The embedded texture of a VS1 character, linear RGB565 (h, w)."""
    m = vs1.read(os.path.join(SRC, name + '.dmf'))
    if m.submeshes:
        t = [t for t in m.textures if t['data']][0]
        fmt, w, h, data = t['format'], t['w'], t['h'], t['data']
    else:
        fmt, w, h, data = m.tex_format, m.tex_w, m.tex_h, m.tex_data
    assert fmt == pvrtex.FMT_RGB565
    return pvrtex.untwiddle(np.frombuffer(data, '<u2'), w, h)


def png_texture(path, size):
    """A KayKit atlas the way VS1 made its character textures (fbx2dmf.convert_texture: box filter, rounded RGB565)."""
    from PIL import Image
    im = Image.open(path).convert('RGBA').resize((size, size), Image.BOX)
    return pvrtex.pack(np.array(im), pvrtex.FMT_RGB565)


def rewrite(m, **over):
    """Our DMF (dmfio.read_dmf dict) back to bytes, with some parts replaced."""
    d = dict(m)
    d.update(over)
    return dmfio.write_dmf(d['verts'], d['groups'], d['batches'], d['strip_words'], d['bones'], d['anims'], d['materials'],
                           d['texture'])


def retexture(model_path, texels):
    m = dmfio.read_dmf(model_path)
    h, w = texels.shape
    tex = dict(m['texture'] or dict(format=dmfio.TEX_RGB565, filter=1))
    tex.update(w=w, h=h, format=dmfio.TEX_RGB565, texels=texels)
    return rewrite(m, texture=tex)


def _vertex_bones(m):
    b0 = np.zeros(len(m['verts']), np.int64)
    b1 = np.zeros(len(m['verts']), np.int64)
    for g in m['groups']:
        b0[g['first']:g['first'] + g['count']] = g['bone0']
        b1[g['first']:g['first'] + g['count']] = g['bone1']
    return b0, b1


def merge_head(vs1_name, body_path, head_bone='head'):
    """VS1's head parts (every triangle rigid on `head_bone`: skull, jaw, glowing eyes, helmet - exactly VS1's
    triangles, strips and texture) on the body of one of our models (every other triangle, with its clips).
    KayKit heads are separate meshes, so no triangle mixes the two sets (asserted)."""
    A = dmfio.read_dmf(convert(vs1_name, clips_from=body_path))
    B = dmfio.read_dmf(body_path)
    names = [b['name'] for b in B['bones']]
    H = names.index(head_bone)
    parts = []                                         # (source model, vertex mask, keep head?)
    out_v, out_key, strips = [], [], []                # strips: (indices into out_v, material tex, batch flags)
    for src, keep_head in ((A, True), (B, False)):
        b0, b1 = _vertex_bones(src)
        is_head = (b0 == H) & (b1 == dmfio.NO_BONE)
        mats = src['materials'] or [dict(tex='@', flags=dmfio.MAT_BILINEAR)]
        sel = np.nonzero(is_head == keep_head)[0]
        base = len(out_v)
        remap = -np.ones(len(src['verts']), np.int64)
        remap[sel] = base + np.arange(len(sel))
        untex = np.zeros(len(src['verts']), bool)
        for k, bt in enumerate(src['batches']):
            mt = mats[bt['flags'] >> 8]
            for st in dmfio.strips_of_batch(src, k):
                h = is_head[st]
                assert h.all() or not h.any(), 'a strip mixes head and body'
                if h[0] != keep_head:
                    continue
                if mt['tex'] == '':
                    untex[st] = True
                strips.append((remap[st], mt['tex'], bt['flags'] & 0xFF))
        for i in sel:
            out_v.append(src['verts'][i])
            # group key; inside a group the untextured (eye) vertices come last, contiguous (battle3d_draw finds
            # the eyes as the untextured batch's index range)
            out_key.append((int(b0[i]), int(b1[i]), int(untex[i]), len(out_key)))
    order = sorted(range(len(out_v)), key=lambda i: out_key[i])
    pos = np.empty(len(order), np.int64)
    pos[order] = np.arange(len(order))
    verts = np.array([out_v[i] for i in order], dmfio.VTX)
    groups = []
    for i, j in enumerate(order):
        k = out_key[j][:2]
        if groups and (groups[-1][2], groups[-1][3]) == k:
            groups[-1][1] += 1
        else:
            groups.append([i, 1, k[0], k[1], 0])
    groups = np.array([tuple(g) for g in groups], dmfio.GRP)
    materials = [dict(tex='@', flags=dmfio.MAT_BILINEAR), dict(tex='', flags=0)]
    words, batches = [], []
    for mi, tex in enumerate(('@', '')):
        for fl in sorted({f for _, t, f in strips if t == tex}):
            sel = [st for st, t, f in strips if t == tex and f == fl]
            first = len(words)
            for st in sel:
                words.append(len(st))
                words.extend(int(x) for x in pos[st])
            batches.append(dict(first_word=first, num_words=len(words) - first, num_strips=len(sel),
                                flags=(mi << 8) | fl, num_tris=sum(len(st) - 2 for st in sel)))
    return dmfio.write_dmf(verts, groups, batches, np.array(words, '<u2'), B['bones'], B['anims'], materials, A['texture'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name', help='VS1 character (assets/vs1_art/models/<name>.dmf) or, with --retexture, our .dmf')
    ap.add_argument('out')
    ap.add_argument('--clips-from', help='copy the embedded clips (and rest pose) of this .dmf of ours')
    ap.add_argument('--body', help='merge: VS1 head parts on the body of this .dmf of ours (its clips)')
    ap.add_argument('--retexture', help='only replace the texture of NAME (our .dmf): vs1:<character> or png:<path>:<size>')
    a = ap.parse_args()
    if a.retexture:
        kind, _, rest = a.retexture.partition(':')
        texels = vs1_texture(rest) if kind == 'vs1' else png_texture(*rest.rsplit(':', 1)[:1], int(rest.rsplit(':', 1)[1]))
        data = retexture(a.name, texels)
    elif a.body:
        data = merge_head(a.name, a.body)
    else:
        data = convert(a.name, a.clips_from)
    open(a.out, 'wb').write(data)
    m = dmfio.read_dmf(a.out)
    print('%s: %d verts, %d tris, %d bones, %d anims, %dx%d texture, %d bytes' % (
        a.out, len(m['verts']), sum(b['num_tris'] for b in m['batches']), len(m['bones']), len(m['anims']),
        m['texture']['w'], m['texture']['h'], len(data)))


if __name__ == '__main__':
    main()
