#!/usr/bin/env python3
"""check_props.py - the converted VS1 village props and textures (tools/vs1art/import_props.py) against VS1's files,
read with VS1's own reader (tools/vs1art/dmf_format.py):

  * every triangle of every prop, drawn the way the field draws it (mat_trs_y(yaw) of the un-rotated model),
    lands exactly where VS1 put it (positions within 1e-5), with VS1's UVs, baked colour and winding;
  * every submesh keeps its texture (VS1 name -> our /rd/tex/ name), bilinear flag and blending;
  * every RGB565 texture survives the PNG round trip bit for bit through the loader's conversion (KOS png_to_img:
    r >> 3, g >> 2, b >> 3); the ARGB4444 vine becomes ARGB1555 (alpha >= 128): reported.

    python3 tests/vs1art/check_props.py
"""
import os, sys
import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'vs1art'))
import dmfio  # noqa: E402
import dmf_format as vs1  # noqa: E402
import pvrtex  # noqa: E402
import import_props as ip  # noqa: E402


def main():
    bad, lines = 0, []
    for name in ip.PROPS:
        a = vs1.read(os.path.join(ip.SRC, 'models', 'village', name + '.dmf'))
        m = dmfio.read_dmf(os.path.join(ROOT, 'romdisk', 'models', name + '.dmf'))
        yaw = np.radians(float(name.rsplit('_r', 1)[1]))
        c, s = float(np.round(np.cos(yaw))), float(np.round(np.sin(yaw)))
        v = m['verts']
        px = c * v['x'].astype(float) + s * v['z'].astype(float)          # mat_trs_y(yaw), as field3d draws it
        pz = -s * v['x'].astype(float) + c * v['z'].astype(float)
        key_b = lambda i, tex, fl: (round(px[i], 5), round(float(v['y'][i]), 5), round(pz[i], 5), round(float(v['u'][i]), 5),
                                    round(float(v['v'][i]), 5), int(v['argb'][i]) & 0xFFFFFF, tex, fl)
        B = []
        for k, bt in enumerate(m['batches']):
            mt = m['materials'][bt['flags'] >> 8]
            fl = (mt['flags'] & dmfio.MAT_BILINEAR != 0, mt['flags'] & dmfio.MAT_ALPHA != 0, bt['flags'] & dmfio.BATCH_TRANSLUCENT != 0)
            for st in dmfio.strips_of_batch(m, k):
                for t in dmfio.strip_triangles(st):
                    B.append(tuple(key_b(i, mt['tex'], fl) for i in t))
        va = a.vertices
        lit = va['n'].view(np.uint8)
        A = []
        for strips, ti, sfl, _ in a.submeshes:
            tex = '/rd/tex/%s.png' % ip.TEX_NAMES[a.textures[ti]['name']]
            fl = (sfl & vs1.SUB_BILINEAR != 0, sfl & vs1.SUB_BLEND != 0, sfl & vs1.SUB_BLEND != 0)
            for st in strips:
                for j in range(len(st) - 2):
                    t = (st[j], st[j + 1], st[j + 2]) if j % 2 == 0 else (st[j + 1], st[j], st[j + 2])
                    if len(set(t)) < 3:
                        continue
                    A.append(tuple((round(float(va['pos'][i][0]), 5), round(float(va['pos'][i][1]), 5), round(float(va['pos'][i][2]), 5),
                                    round(float(va['uv'][i][0]), 5), round(float(va['uv'][i][1]), 5),
                                    int(lit[i][0]) << 16 | int(lit[i][1]) << 8 | int(lit[i][2]), tex, fl) for i in t))
        norm = lambda T: sorted(tuple(t[t.index(min(t)):] + t[:t.index(min(t))]) for t in T)
        ok = norm(A) == norm(B) and (m['flags'] & dmfio.PRELIT)
        bad += not ok
        lines.append('%s prop %-18s %5d triangles at VS1\'s world positions, UVs, baked colours, textures, winding' % ('ok  ' if ok else 'FAIL', name, len(A)))
    for src, dst in sorted(ip.TEX_NAMES.items()):
        fmt, w, h, _, data = vs1.read_dtx(os.path.join(ip.SRC, 'textures', src + '.dtx'))
        t = pvrtex.untwiddle(np.frombuffer(data, '<u2'), w, h).astype(np.int64)
        im = np.array(Image.open(os.path.join(ROOT, 'romdisk', 'tex', dst + '.png')).convert('RGBA')).astype(np.int64)
        if fmt == pvrtex.FMT_RGB565:
            loaded = (im[..., 0] >> 3) << 11 | (im[..., 1] >> 2) << 5 | (im[..., 2] >> 3)     # KOS png_to_img, PNG_NO_ALPHA
            ok = np.array_equal(loaded, t)
            bad += not ok
            lines.append('%s tex  %-10s %3dx%-3d RGB565 bit for bit after the PNG round trip' % ('ok  ' if ok else 'FAIL', dst + '.png', w, h))
        else:
            a4 = (t >> 12) & 15
            rgb_ok = np.array_equal(im[..., 0] >> 4, (t >> 8) & 15) and np.array_equal(im[..., 1] >> 4, (t >> 4) & 15) and np.array_equal(im[..., 2] >> 4, t & 15)
            changed = int(((a4 * 17 >= 128) != (im[..., 3] >= 128)).sum())
            ok = rgb_ok and changed == 0
            bad += not ok
            lines.append('%s tex  %-10s %3dx%-3d ARGB4444 -> the loader\'s ARGB1555: colour kept, alpha thresholded at 50 %% (%d of %d texels partly transparent in VS1)' % (
                'ok  ' if ok else 'FAIL', dst + '.png', w, h, int(((a4 > 0) & (a4 < 15)).sum()), w * h))
    print('\n'.join(lines))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
