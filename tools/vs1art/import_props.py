#!/usr/bin/env python3
"""import_props.py - the VS1 village art (assets/vs1_art/, see its README) into this runtime's formats.

VS1 (the sibling lineage, larp3-2) built its village from the same Quaternius Medieval Village kit and Poly
Haven boulders as ours, but with a richer recipe (windows + shutters on the camera-facing walls, generated
gable walls, a warm lit-window glass texture, less aggressive decimation, 128x128 textures, lighting baked
into the vertices). Its files are a different DMF variant (24-byte vertices, pre-twiddled .dtx textures,
per-yaw variants pre-rotated into world orientation). This tool converts, losslessly:

  textures   assets/vs1_art/textures/<name>.dtx -> romdisk/tex/<ours>.png   (untwiddled; the RGB565 values
             survive the PNG round trip exactly: the loader truncates, the expansion below rounds)
             rooftiles -> roof, stonewall -> ubrick (our names: the battle arena's houses use the same
             kit textures with the same UVs, so they pick the sharper 128x128 versions up for free)
  props      assets/vs1_art/models/village/<base>_r<yaw>.dmf -> romdisk/models/<base>_r<yaw>.dmf (our DMF v2)
             * geometry un-rotated by our mat_trs_y(-yaw): the field draws `prop <base> x y <yaw>` with
               mat_trs_y(yaw), which puts every vertex back exactly where VS1 had it
             * baked lighting -> vertex argb, header flag PRELIT (field3d uses it as the final colour;
               the normals are zero: the VS1 files store the bake in the normal bytes)
             * submeshes -> batches with one material each (shared /rd/tex/*.png; BLEND -> MAT_ALPHA +
               translucent batch; BILINEAR kept); double-sided, as VS1 drew them (its PVR culling is off
               and its camera-cone culling assumed so)
             * VS1's k-DOP occluders are dropped (our fade test is screen-space, field3d.cpp)

    python3 tools/vs1art/import_props.py [--check]      (--check: convert to memory, compare with romdisk)
Deterministic: the same inputs give byte-identical outputs (tests/vs1art/run.sh checks it).
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
import dmf_format as vs1  # noqa: E402
import pvrtex  # noqa: E402
import dmfio  # noqa: E402

SRC = os.path.join(ROOT, 'assets', 'vs1_art')
TEX_NAMES = {   # VS1 texture -> romdisk/tex/<name>.png
    'boulder02': 'boulder02', 'boulder03': 'boulder03', 'brick': 'brick', 'glass': 'glass', 'plaster': 'plaster',
    'rocktrim': 'rocktrim', 'rooftiles': 'roof', 'stonewall': 'ubrick', 'vine': 'vine', 'wood': 'wood',
}
PROPS = ['arch_r90', 'border_r0', 'boulder02_r0', 'boulder02_r90', 'boulder03_r0', 'boulder03_r90', 'crate_r0',
         'fence_r0', 'house_plaster_r0', 'house_plaster_r180', 'house_stone_r0', 'house_stone_r180', 'wagon_r0']


def texture_png(name):
    """VS1 .dtx -> PIL image (RGB for RGB565, RGBA for the alpha formats)."""
    fmt, w, h, _, data = vs1.read_dtx(os.path.join(SRC, 'textures', name + '.dtx'))
    texels = pvrtex.untwiddle(np.frombuffer(data, '<u2'), w, h)
    rgba = pvrtex.unpack(texels, fmt)
    img = Image.fromarray(rgba, 'RGBA')
    return (img if fmt != pvrtex.FMT_RGB565 else img.convert('RGB')), fmt


def convert_prop(name):
    m = vs1.read(os.path.join(SRC, 'models', 'village', name + '.dmf'))
    assert m.flags & vs1.FLAG_PRELIT and not len(m.bones) and m.submeshes, name
    yaw = np.radians(float(name.rsplit('_r', 1)[1]))
    assert float(name.rsplit('_r', 1)[1]) % 90 == 0, name
    # our mat_trs_y(yaw): x' = c x + s z, z' = -s x + c z; the stored model is R(-yaw) applied to VS1's
    c, s = np.cos(-yaw), np.sin(-yaw)
    c, s = float(np.round(c)), float(np.round(s))   # the VS1 variants are quarter turns: exact 0 / +-1
    p = m.vertices['pos'].astype(np.float64)
    x = c * p[:, 0] + s * p[:, 2]
    z = -s * p[:, 0] + c * p[:, 2]
    vt = np.zeros(len(m.vertices), dmfio.VTX)
    vt['x'], vt['y'], vt['z'] = x.astype(np.float32), m.vertices['pos'][:, 1], z.astype(np.float32)
    vt['u'], vt['v'] = m.vertices['uv'][:, 0], m.vertices['uv'][:, 1]
    lit = m.vertices['n'].view(np.uint8).astype(np.uint32)              # PRELIT: baked R, G, B
    vt['argb'] = 0xFF000000 | (lit[:, 0] << 16) | (lit[:, 1] << 8) | lit[:, 2]
    vt['w0'] = 255
    groups = np.zeros(1, dmfio.GRP)
    groups[0] = (0, len(vt), dmfio.NO_BONE, dmfio.NO_BONE, 0)
    materials, batches, words = [], [], []
    for strips, t, fl, argb in m.submeshes:
        assert argb == 0xFFFFFFFF and t >= 0, (name, 'untextured/unlit submeshes are not used by props')
        tex = m.textures[t]['name']
        mflags = (dmfio.MAT_BILINEAR if fl & vs1.SUB_BILINEAR else 0) | (dmfio.MAT_ALPHA if fl & vs1.SUB_BLEND else 0)
        mat = dict(tex='/rd/tex/%s.png' % TEX_NAMES[tex], flags=mflags)
        if mat not in materials:
            materials.append(mat)
        mi = materials.index(mat)
        first = len(words)
        ntris = 0
        for st in strips:
            words.append(len(st))
            words.extend(st)
            ntris += len(st) - 2                        # the runtime's count (degenerate stitches included)
        bflags = (mi << 8) | dmfio.BATCH_DOUBLE_SIDED | (dmfio.BATCH_TRANSLUCENT if fl & vs1.SUB_BLEND else 0)
        batches.append(dict(first_word=first, num_words=len(words) - first, num_strips=len(strips), flags=bflags,
                            num_tris=ntris))
    assert len(materials) <= 8, name
    return dmfio.write_dmf(vt, groups, batches, np.array(words, '<u2'), (), (), materials, None, extra_flags=dmfio.PRELIT)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='compare with the files in romdisk/ instead of writing')
    a = ap.parse_args()
    bad = 0
    for src, dst in sorted(TEX_NAMES.items()):
        img, fmt = texture_png(src)
        path = os.path.join(ROOT, 'romdisk', 'tex', dst + '.png')
        if a.check:
            same = os.path.exists(path) and np.array_equal(np.array(Image.open(path)), np.array(img))
            bad += not same
            print('%-4s tex %s <- %s.dtx' % ('ok' if same else 'DIFF', dst, src))
        else:
            img.save(path, optimize=True)
            print('tex %-10s <- %-10s %dx%d %s' % (dst + '.png', src + '.dtx', img.size[0], img.size[1], pvrtex.FMT_NAMES[fmt]))
    for name in PROPS:
        data = convert_prop(name)
        path = os.path.join(ROOT, 'romdisk', 'models', name + '.dmf')
        if a.check:
            same = os.path.exists(path) and open(path, 'rb').read() == data
            bad += not same
            print('%-4s model %s' % ('ok' if same else 'DIFF', name))
        else:
            open(path, 'wb').write(data)
            m = dmfio.read_dmf(path)
            print('model %-20s %5d verts %5d tris %2d batches %6d bytes' % (
                name, len(m['verts']), sum(b['num_tris'] for b in m['batches']), len(m['batches']), len(data)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
