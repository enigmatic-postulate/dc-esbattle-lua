#!/usr/bin/env python3
"""check_ground.py - the field's ground texture (field3d.cpp build_ground_texture) against an independent Python
transcription of VS1's generator (larp3-2/src/field3d.cpp field3d_on_map_loaded), bit for bit over the map region,
and the padding = the outer ground colour.

    python3 tests/vs1art/check_ground.py DUMP_DIR/frame_NNNNN.txt MAP.txt
The dump is any field frame (battle_trace --dump); the ground is its 256x128 RGB565 memory texture.
"""
import os, re, sys
import numpy as np

GRASS, FLOWERS, PATH, WALL, TREE, WATER = 0, 1, 2, 3, 4, 5
CH = {'.': GRASS, ',': FLOWERS, '=': PATH, '#': WALL, 'T': TREE, '~': WATER}
f32 = np.float32


def h32(x, y):                      # VS1 hash2, uint32 arithmetic
    h = (x * 374761393 + y * 668265263) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return h ^ (h >> 16)


def noise(x, y):                    # VS1's value noise, in single precision as the C++ (x, y: float32)
    xi, yi = int(np.floor(x)), int(np.floor(y))
    fx, fy = f32(x - f32(xi)), f32(y - f32(yi))
    sx = f32(f32(fx * fx) * f32(f32(3) - f32(f32(2) * fx)))
    sy = f32(f32(fy * fy) * f32(f32(3) - f32(f32(2) * fy)))
    h = lambda a, b: f32(f32(h32(a & 0xFFFFFFFF, b & 0xFFFFFFFF) & 1023) / f32(1023.0))
    a = f32(h(xi, yi) + f32(f32(h(xi + 1, yi) - h(xi, yi)) * sx))
    b = f32(h(xi, yi + 1) + f32(f32(h(xi + 1, yi + 1) - h(xi, yi + 1)) * sx))
    return f32(a + f32(f32(b - a) * sy))


def rgb565(rgb, pct):
    r, g, b = (rgb >> 16 & 255) * pct // 100, (rgb >> 8 & 255) * pct // 100, (rgb & 255) * pct // 100
    r, g, b = min(r, 255), min(g, 255), min(b, 255)
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


def reference(tiles, w, h, tpt):
    def tile_of(tx, ty):
        return tiles[min(max(ty, 0), h - 1)][min(max(tx, 0), w - 1)]
    out = {}
    for y in range(h * tpt):
        for x in range(w * tpt):
            tx, ty, sx, sy = x // tpt, y // tpt, x % tpt, y % tpt
            t = tile_of(tx, ty)
            hsh = h32(x, y)
            wx = f32(f32(f32(x) + f32(0.5)) / f32(tpt)); wy = f32(f32(f32(y) + f32(0.5)) / f32(tpt))
            n = noise(f32(wx / f32(3.0)), f32(wy / f32(3.0)))
            pct = 86 + int(f32(n * f32(22.0))) + (hsh % 7) - 3
            if t == FLOWERS:
                c = (0xF4F4F4 if (hsh >> 12) & 1 else 0xFFCD75) if (hsh >> 8) % 11 == 0 else 0x3E9E55
            elif t == PATH: c = 0xC9A46A
            elif t == WALL: c = 0x94B0C2
            elif t == TREE: c = 0x1E6B48
            elif t == WATER: c = 0x73EFF7 if (hsh >> 8) % 13 == 0 else 0x3B5DC9
            else: c = 0x3E9E55
            isp = lambda v: v == PATH
            mixed = (sx == 0 and isp(tile_of(tx - 1, ty)) != isp(t)) or (sx == tpt - 1 and isp(tile_of(tx + 1, ty)) != isp(t)) or \
                    (sy == 0 and isp(tile_of(tx, ty - 1)) != isp(t)) or (sy == tpt - 1 and isp(tile_of(tx, ty + 1)) != isp(t))
            if mixed and (hsh >> 4) % 3 != 0:
                o = 0x3E9E55 if isp(t) else 0xC9A46A
                c = (((c >> 16 & 255) + (o >> 16 & 255)) // 2) << 16 | (((c >> 8 & 255) + (o >> 8 & 255)) // 2) << 8 | ((c & 255) + (o & 255)) // 2
            out[(x, y)] = rgb565(c, pct)
    return out


def main():
    dump, mapf = sys.argv[1], sys.argv[2]
    lines = open(mapf).read().split('\n')
    i = next(k for k, l in enumerate(lines) if l.startswith('map '))
    w, h = map(int, lines[i].split()[1:3])
    tiles = [[CH[c] for c in lines[i + 1 + r]] for r in range(h)]
    tex = None
    for ln in open(dump):
        m = re.match(r'tex \d+ mem 256 128 1 (\S+)', ln)
        if m:
            tex = np.fromfile(os.path.join(os.path.dirname(dump), m.group(1)), '<u2').reshape(128, 256)
    if tex is None:
        print('no 256x128 RGB565 ground texture in the dump'); return 1
    tpt = 8
    while tpt > 1 and (w * tpt > 256 or h * tpt > 128):
        tpt //= 2
    ref = reference(tiles, w, h, tpt)
    bad = sum(1 for (x, y), v in ref.items() if tex[y, x] != v)
    outer = rgb565(0x24403A, 100)
    pad = [(x, y) for y in range(128) for x in range(256) if x >= w * tpt or y >= h * tpt]
    badpad = sum(1 for x, y in pad if tex[y, x] != outer)
    if bad or badpad:
        print('ground texture: %d of %d map texels differ from VS1\'s generator, %d of %d padding texels not the outer colour' % (bad, len(ref), badpad, len(pad)))
        return 1
    print('ground texture == VS1\'s generator bit for bit (%dx%d map at %d texels per tile: %d texels), padding (%d texels) = outer ground' % (w, h, tpt, len(ref), len(pad)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
