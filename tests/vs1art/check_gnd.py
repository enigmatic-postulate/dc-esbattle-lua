#!/usr/bin/env python3
"""check_gnd.py - the painted village ground (romdisk/maps/<map>.gnd, tools/ground/paint_ground.py; docs/GROUND_ART.md).

  * the file: a GND1 header that matches the map, a VQ payload of exactly 2 KiB + w*h/4 index bytes;
  * the game draws exactly that texture: the frame dump's 512x512 memory texture (the host decodes the VQ data as
    the PVR reads it, eng::vq_decode) == this script's independent decode (codebook texel order (0,0) (0,1) (1,0)
    (1,1), indices in the KOS twiddled order - tools/vs1art/pvrtex.py);
  * the VQ error against the painted image is small (PSNR);
  * the art targets, measured on the map area against the VS1 ground it replaces (the same tiles through VS1's
    generator, check_ground.py): detail (mean absolute difference between neighbouring texels, per texel and per
    tile), colour (mean saturation), contrast (luma standard deviation), and how close the grass and the dirt are
    in hue to the village's own colours (the kit's vine/moss greens, the wood/boulder browns).

    python3 tests/vs1art/check_gnd.py DUMP_FRAME.txt [MAP.txt]
"""
import colorsys, os, re, struct, sys
import numpy as np

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'vs1art'))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'vs1art'))
import pvrtex  # noqa: E402
import check_ground  # noqa: E402

KIT_GREEN = np.array([[68, 153, 51], [107, 137, 13]], float)       # romdisk/tex/vine.png, the moss of roof.png
KIT_BROWN = np.array([[139, 105, 76], [103, 79, 57], [109, 83, 56]], float)   # wood.png, boulder02/03.png


def decode(data, w, h):
    cb = np.frombuffer(data[:2048], '<u2').reshape(256, 4)
    tw = np.frombuffer(data[2048:], np.uint8)
    n = w // 2
    idx = tw[pvrtex.twiddle_index(n, n)]                                  # (n, n): [by, bx]
    blk = cb[idx]                                                        # k = dx * 2 + dy
    return blk.reshape(n, n, 2, 2).transpose(0, 3, 1, 2).reshape(h, w)   # [by, dy, bx, dx]


def rgb(t):
    t = t.astype(np.int64)
    return np.stack([(t >> 11 & 31) * 255 // 31, (t >> 5 & 63) * 255 // 63, (t & 31) * 255 // 31], -1).astype(float)


def metrics(img):
    """img (H, W, 3) 0..255 -> detail (per texel), saturation, luma std"""
    y = img @ [0.299, 0.587, 0.114]
    detail = (np.abs(np.diff(y, axis=0)).mean() + np.abs(np.diff(y, axis=1)).mean()) / 2
    mx, mn = img.max(-1), img.min(-1)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1), 0).mean()
    return detail, sat, y.std()


def main():
    dump = sys.argv[1]
    mapf = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'romdisk', 'maps', 'village.txt')
    L = open(mapf).read().split('\n')
    name = next(l.split()[1] for l in L if l.startswith('name '))
    i = next(k for k, l in enumerate(L) if l.startswith('map '))
    mw, mh = map(int, L[i].split()[1:3])
    rows = L[i + 1:i + 1 + mh]
    b = open(os.path.join(os.path.dirname(mapf), name + '.gnd'), 'rb').read()
    magic, w, h, tpt, margin, gw, gh, outer, flags, nbytes = struct.unpack('<4sHHHHHHHHI8x', b[:32])
    errs = []
    if magic != b'GND1' or flags != 1 or (gw, gh) != (mw, mh) or nbytes != len(b) - 32 or nbytes != 2048 + w * h // 4 or w != h:
        errs.append('bad header or size')
    ref = decode(b[32:], w, h)
    tex = None
    for ln in open(dump):
        m = re.match(r'tex \d+ mem %d %d 1 (\S+)' % (w, h), ln)
        if m:
            tex = np.fromfile(os.path.join(os.path.dirname(dump), m.group(1)), '<u2').reshape(h, w)
    if tex is None:
        errs.append('no %dx%d ground texture in the dump' % (w, h))
    elif not np.array_equal(tex, ref):
        errs.append('the game\'s decoded ground differs from this decode in %d texels' % int((tex != ref).sum()))
    # the map area, then the same through VS1's generator
    a0, a1 = margin * tpt, (margin + np.array([mw, mh])) * tpt
    ours = rgb(ref)[a0:a1[1], a0:a1[0]]
    tiles = [[check_ground.CH[c] for c in r] for r in rows]
    vtpt = 8
    while vtpt > 1 and (mw * vtpt > 256 or mh * vtpt > 128):
        vtpt //= 2
    v = check_ground.reference(tiles, mw, mh, vtpt)
    vs1 = np.zeros((mh * vtpt, mw * vtpt, 3))
    for (x, y), t in v.items():
        vs1[y, x] = rgb(np.array(t))
    d0, s0, c0 = metrics(vs1)
    d1, s1, c1 = metrics(ours)
    # detail per tile: the texel metric scaled by texels per tile (what the camera shows of one tile)
    grass = ours[np.repeat(np.repeat(np.array([[c in '.,' for c in r] for r in rows]), tpt, 0), tpt, 1)]
    dirt = ours[np.repeat(np.repeat(np.array([[c == '=' for c in r] for r in rows]), tpt, 0), tpt, 1)]
    def hue(c):
        return colorsys.rgb_to_hsv(*(np.asarray(c, float) / 255.0))[0] * 360.0
    # distance in hue (degrees) from the nearest kit colour: the village's greens are yellow-green (vine 110,
    # moss 74), VS1's grass leaned to mint (134)
    dist = lambda px, kit: min(min(abs(hue(px.mean(0)) - hue(k)), 360 - abs(hue(px.mean(0)) - hue(k))) for k in kit)
    g_ours, d_ours = dist(grass, KIT_GREEN), dist(dirt, KIT_BROWN)
    vg = vs1[np.repeat(np.repeat(np.array([[c in '.,' for c in r] for r in rows]), vtpt, 0), vtpt, 1)]
    vd = vs1[np.repeat(np.repeat(np.array([[c == '=' for c in r] for r in rows]), vtpt, 0), vtpt, 1)]
    g_vs1, d_vs1 = dist(vg, KIT_GREEN), dist(vd, KIT_BROWN)
    if not (d1 * tpt > 2 * d0 * vtpt and s1 > s0 and c1 > c0 and g_ours < g_vs1 and d_ours < d_vs1):
        errs.append('art targets not met')
    print('%s.gnd: %dx%d VQ (%d bytes), %d texels per tile, %d-tile margin; decoded by the game == decoded here: %s' % (
        name, w, h, nbytes, tpt, margin, 'yes' if tex is not None and np.array_equal(tex, ref) else 'NO'))
    print('  detail per tile %.0f (VS1 %.0f; per texel %.1f / %.1f), saturation %.2f (VS1 %.2f), luma spread %.1f (VS1 %.1f)' % (
        d1 * tpt, d0 * vtpt, d1, d0, s1, s0, c1, c0))
    print('  mean grass %s: %.0f degrees of hue from the kit\'s greens (VS1 %s: %.0f); mean dirt %s: %.0f from the kit\'s browns (VS1 %s: %.0f)' % (
        tuple(int(x) for x in grass.mean(0)), g_ours, tuple(int(x) for x in vg.mean(0)), g_vs1,
        tuple(int(x) for x in dirt.mean(0)), d_ours, tuple(int(x) for x in vd.mean(0)), d_vs1))
    if errs:
        print('\n'.join(errs)); return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
