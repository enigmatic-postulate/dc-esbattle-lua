#!/usr/bin/env python3
"""check_chars.py - the shipped characters are exactly "VS1's head parts on our body" (tools/vs1art/import_chars.py).

For each of knight, skel_warrior, skel_mage, skel_rogue, wren, brom:
  * its head triangles (every vertex rigid on `head`) == VS1's head triangles (positions, UVs, normals, winding);
  * its other triangles == the body model's (assets/vs1_art/bodies/) non-head triangles;
  * skeleton, rest pose and embedded clips == the body model's (the field and battle animation is unchanged);
  * the texture == VS1's 128x128 texels; the glowing eyes (if any) are VS1's cyan and contiguous (battle3d_draw);
  * skinning in the bind pose and in an idle pose moves both halves together (no seam between the sources).

    python3 tests/vs1art/check_chars.py [ROMDISK_MODELS]
"""
import os, sys
import numpy as np

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'vs1art'))
import dmfio  # noqa: E402
import import_chars  # noqa: E402

CAST = [('knight', 'hero'), ('skel_warrior', 'skeleton'), ('skel_mage', 'skeleton_mage'), ('skel_rogue', 'skeleton_rogue'),
        ('wren', 'mage'), ('brom', 'barbarian'), ('corwin', 'ranger')]   # slice 3D-6: Corwin (the Ranger)


def tris(m, head_sel):
    """Triangles (canonical rotation, winding kept) of the head (head_sel True) or the body, as hashable tuples."""
    names = [b['name'] for b in m['bones']]
    H = names.index('head')
    b0, b1 = import_chars._vertex_bones(m)
    is_head = (b0 == H) & (b1 == dmfio.NO_BONE)
    v = m['verts']
    key = lambda i: (round(float(v['x'][i]), 5), round(float(v['y'][i]), 5), round(float(v['z'][i]), 5),
                     round(float(v['u'][i]), 5), round(float(v['v'][i]), 5), int(v['nx'][i]), int(v['ny'][i]), int(v['nz'][i]),
                     int(v['argb'][i]), int(b0[i]), int(b1[i]), int(v['w0'][i]) if b1[i] != dmfio.NO_BONE else 0)
    out = []
    for k in range(len(m['batches'])):
        for st in dmfio.strips_of_batch(m, k):
            for t in dmfio.strip_triangles(st):
                if bool(is_head[t[0]]) != head_sel:
                    continue
                c = [key(i) for i in t]
                r = c.index(min(c))
                out.append(tuple(c[r:] + c[:r]))
    return sorted(out)


def converter_fidelity(vs1_name):
    """import_chars.convert() against VS1's own reader (tools/vs1art/dmf_format.py): every triangle's positions, UVs,
    normals, bone pair and weight (ours w0 = 255 - VS1's w1, bone indices minus the dropped Rig_Medium root)."""
    import dmf_format as vs1
    a = vs1.read(os.path.join(ROOT, 'assets', 'vs1_art', 'models', vs1_name + '.dmf'))
    ga = np.zeros((len(a.vertices), 2), np.int64)
    for g in a.groups:
        ga[g['first']:g['first'] + g['count']] = (g['bone0'], g['bone1'])
    drop = 1 if a.bones[0]['name'] == b'Rig_Medium' else 0
    va = a.vertices
    def ka(i):
        b1 = int(ga[i, 1])
        return (tuple(np.round(va['pos'][i].astype(float), 5)), tuple(np.round(va['uv'][i].astype(float), 5)), tuple(int(x) for x in va['n'][i]),
                int(ga[i, 0]) - drop, 255 if b1 == 255 else b1 - drop, 255 - int(va['w1'][i]) if b1 != 255 else 0)
    A = sorted(tuple(ka(i) for i in t) for t in a.triangles() if len(set(t)) == 3)
    m = dmfio.read_dmf(import_chars.convert(vs1_name))
    b0, b1 = import_chars._vertex_bones(m)
    v = m['verts']
    def kb(i):
        return ((round(float(v['x'][i]), 5), round(float(v['y'][i]), 5), round(float(v['z'][i]), 5)), (round(float(v['u'][i]), 5), round(float(v['v'][i]), 5)),
                (int(v['nx'][i]), int(v['ny'][i]), int(v['nz'][i])), int(b0[i]), int(b1[i]), int(v['w0'][i]) if b1[i] != 255 else 0)
    B = sorted(tuple(kb(i) for i in t) for k in range(len(m['batches'])) for st in dmfio.strips_of_batch(m, k) for t in dmfio.strip_triangles(st))
    return A == B, len(A)


def main():
    models = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'romdisk', 'models')
    bad, lines = 0, []
    for vs1 in ('hero', 'skeleton', 'mage', 'skeleton_mage', 'skeleton_rogue', 'barbarian'):
        same, n = converter_fidelity(vs1)
        bad += not same
        lines.append('%s converter: VS1 %s -> our DMF keeps every triangle (%d: positions, UVs, normals, bones, weights)' % ('ok  ' if same else 'FAIL', vs1, n))
    for ours, vs1 in CAST:
        m = dmfio.read_dmf(os.path.join(models, ours + '.dmf'))
        body_path = os.path.join(ROOT, 'assets', 'vs1_art', 'bodies', ours + '.dmf')
        body = dmfio.read_dmf(body_path)
        src = dmfio.read_dmf(import_chars.convert(vs1, clips_from=body_path))
        errs = []
        if tris(m, True) != tris(src, True):
            errs.append('head triangles differ from VS1')
        if tris(m, False) != tris(body, False):
            errs.append('body triangles differ from the body model')
        if [b['name'] for b in m['bones']] != [b['name'] for b in body['bones']] or any(
                not (np.array_equal(a['t'], b['t']) and np.array_equal(a['q'], b['q']) and np.array_equal(a['inv_bind'], b['inv_bind']))
                for a, b in zip(m['bones'], body['bones'])):
            errs.append('skeleton differs from the body model')
        if len(m['anims']) != len(body['anims']) or any(
                a['name'] != b['name'] or not np.array_equal(a['q'], b['q']) or not np.array_equal(a['t'], b['t'])
                for a, b in zip(m['anims'], body['anims'])):
            errs.append('clips differ from the body model')
        if not np.array_equal(m['texture']['texels'], import_chars.vs1_texture(vs1)):
            errs.append('texture is not VS1\'s')
        eyes = ''
        mats = m['materials']
        untex = [k for k, bt in enumerate(m['batches']) if mats and mats[bt['flags'] >> 8]['tex'] == '']
        if untex:
            idx = sorted({i for k in untex for st in dmfio.strips_of_batch(m, k) for i in st})
            if idx != list(range(idx[0], idx[-1] + 1)):
                errs.append('eye vertices not contiguous')
            cols = {int(c) for c in m['verts']['argb'][idx]}
            if cols != {0xFF78FFE6}:
                errs.append('eye colours %s' % [hex(c) for c in cols])
            eyes = ', %d eye vertices (78FFE6)' % len(idx)
        # no seam: posed with an idle of the library, the head follows the head bone exactly as in the VS1 model
        lib = dmfio.read_dma(os.path.join(ROOT, 'romdisk', 'anims', 'rig_medium.dma'))
        clip = [c for c in lib['clips'] if c['name'] == ('skel_idle' if ours.startswith('skel') else 'idle_a')][0]
        remap = [lib['bones'].index(b['name']) for b in m['bones']]
        qs, ts = dmfio.sample_clip(m, clip, 0.4, remap)
        P = dmfio.skinned_positions(m, dmfio.skin_mats(m, qs, ts))[0]
        qs2, ts2 = dmfio.sample_clip(src, clip, 0.4, remap)
        P2 = dmfio.skinned_positions(src, dmfio.skin_mats(src, qs2, ts2))[0]
        hk = lambda mm, PP: sorted(map(tuple, np.round(PP[(import_chars._vertex_bones(mm)[0] == [b['name'] for b in mm['bones']].index('head'))
                                                           & (import_chars._vertex_bones(mm)[1] == dmfio.NO_BONE)], 4)))
        if hk(m, P) != hk(src, P2):
            errs.append('posed head differs from VS1\'s posed head')
        nt = sum(b['num_tris'] for b in m['batches'])
        if errs:
            bad += 1
            lines.append('FAIL %s: %s' % (ours, '; '.join(errs)))
        else:
            lines.append('ok   %s = VS1 %s head (%d tris) + our body (%d tris): %d verts%s' % (
                ours, vs1, len(tris(m, True)), len(tris(m, False)), len(m['verts']), eyes))
    print('\n'.join(lines))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
