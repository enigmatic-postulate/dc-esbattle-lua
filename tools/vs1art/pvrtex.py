"""VENDORED from the VS1 lineage (larp3-2/tools/dmf/pvrtex.py), unchanged below this note."""
"""PVR texture helpers: RGB565 / ARGB1555 / ARGB4444 packing and PowerVR2 twiddling.

Twiddle order is the one KOS uses (pvr_texture.c: TWIDOUT(x, y) = TWIDTAB(y) | TWIDTAB(x) << 1),
i.e. a Morton order with v (row) in the even bits and u (column) in the odd bits. For
non-square textures the square of side min(w, h) is twiddled and the squares follow each other.
"""
import numpy as np

FMT_NONE, FMT_RGB565, FMT_ARGB1555, FMT_ARGB4444 = 0, 1, 2, 3
FMT_NAMES = {FMT_NONE: "none", FMT_RGB565: "rgb565", FMT_ARGB1555: "argb1555", FMT_ARGB4444: "argb4444"}


def _spread(v):
    """Spreads the bits of v so bit i moves to bit 2i (TWIDTAB)."""
    v = np.asarray(v, dtype=np.uint32)
    r = np.zeros_like(v)
    for b in range(11):
        r |= ((v >> b) & 1) << (2 * b)
    return r


def twiddle_index(w, h):
    """Returns idx[y, x] = position of texel (x, y) in the twiddled stream."""
    m = min(w, h)
    ys, xs = np.mgrid[0:h, 0:w].astype(np.uint32)
    base = _spread(ys & (m - 1)) | (_spread(xs & (m - 1)) << 1)
    block = (xs // m + ys // m) * m * m
    return (base + block).astype(np.int64)


def twiddle(texels):
    """texels: (h, w) uint16 -> flat twiddled uint16 array."""
    h, w = texels.shape
    assert w & (w - 1) == 0 and h & (h - 1) == 0, "texture sides must be powers of two"
    out = np.zeros(w * h, dtype=np.uint16)
    out[twiddle_index(w, h).ravel()] = texels.ravel()
    return out


def untwiddle(stream, w, h):
    return np.asarray(stream, dtype=np.uint16)[twiddle_index(w, h)]


def pack(rgba, fmt):
    """rgba: (h, w, 4) uint8 -> (h, w) uint16 in the PVR format (rounded, not truncated)."""
    c = rgba.astype(np.uint32)
    q = lambda ch, bits: (c[..., ch] * ((1 << bits) - 1) + 127) // 255
    if fmt == FMT_RGB565:
        return ((q(0, 5) << 11) | (q(1, 6) << 5) | q(2, 5)).astype(np.uint16)
    if fmt == FMT_ARGB1555:
        return (((c[..., 3] >= 128).astype(np.uint32) << 15) | (q(0, 5) << 10) | (q(1, 5) << 5) | q(2, 5)).astype(np.uint16)
    if fmt == FMT_ARGB4444:
        return ((q(3, 4) << 12) | (q(0, 4) << 8) | (q(1, 4) << 4) | q(2, 4)).astype(np.uint16)
    raise ValueError(fmt)


def unpack(t, fmt):
    """(h, w) uint16 -> (h, w, 4) uint8 (for previews / round-trip tests)."""
    t = t.astype(np.uint32)
    ex = lambda v, bits: (v * 255 + ((1 << bits) - 1) // 2) // ((1 << bits) - 1)
    if fmt == FMT_RGB565:
        r, g, b, a = ex(t >> 11 & 31, 5), ex(t >> 5 & 63, 6), ex(t & 31, 5), np.full_like(t, 255)
    elif fmt == FMT_ARGB1555:
        r, g, b, a = ex(t >> 10 & 31, 5), ex(t >> 5 & 31, 5), ex(t & 31, 5), (t >> 15) * 255
    elif fmt == FMT_ARGB4444:
        r, g, b, a = ex(t >> 8 & 15, 4), ex(t >> 4 & 15, 4), ex(t & 15, 4), ex(t >> 12 & 15, 4)
    else:
        raise ValueError(fmt)
    return np.stack([r, g, b, a], -1).astype(np.uint8)
