"""raster.py - Python side of raster.c (PVR-like software rasterizer) + the renderer's camera math.

    c = Canvas(640, 480, 0x1A2B22)
    c.tris(rv, tex=rgba_or_None, bilinear=True, mode=OP, cull=True)   # rv: (N*3,) RV records
    c.save('out.png')

Camera math mirrors src/render3d.cpp camera_look(): view = look_at(eye, target, up = +Y), screen
x = 320 + f*x_view/W, y = 240 - f*y_view/W with W = -z_view and f = 240 / tan(fov_y / 2); z = 1/W.
"""
import ctypes
import os
import subprocess
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
_LIB = None
OP, BLEND, ADD, MUL = 0, 1, 2, 3
RV = np.dtype([('x', '<f4'), ('y', '<f4'), ('z', '<f4'), ('u', '<f4'), ('v', '<f4'), ('argb', '<u4')])


def _lib():
    global _LIB
    if _LIB is None:
        so = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'battle3d_raster_%d.so' % os.getuid())
        src = os.path.join(HERE, 'raster.c')
        if not os.path.exists(so) or os.path.getmtime(so) < os.path.getmtime(src):
            subprocess.check_call(['cc', '-O2', '-shared', '-fPIC', src, '-o', so, '-lm'])
        _LIB = ctypes.CDLL(so)
        _LIB.raster_tris.restype = None
        _LIB.raster_fog.restype = None
    return _LIB


class Canvas:
    def __init__(self, w=640, h=480, clear=0x000000):
        self.w, self.h = w, h
        self.color = np.zeros((h, w, 3), np.float32)
        self.color[:] = [(clear >> 16) & 255, (clear >> 8) & 255, clear & 255]
        self.depth = np.zeros((h, w), np.float32)

    def tris(self, rv, tex=None, bilinear=True, mode=OP, cull=False):
        rv = np.ascontiguousarray(rv, RV)
        n = len(rv) // 3
        if n == 0:
            return
        if tex is not None:
            tex = np.ascontiguousarray(tex, np.uint8)
            tp, tw, th = tex.ctypes.data_as(ctypes.c_void_p), tex.shape[1], tex.shape[0]
        else:
            tp, tw, th = None, 1, 1
        _lib().raster_tris(rv.ctypes.data_as(ctypes.c_void_p), ctypes.c_int(n), tp, ctypes.c_int(tw), ctypes.c_int(th),
                           ctypes.c_int(1 if bilinear else 0), ctypes.c_int(mode), ctypes.c_int(int(cull)),
                           self.color.ctypes.data_as(ctypes.c_void_p), self.depth.ctypes.data_as(ctypes.c_void_p),
                           ctypes.c_int(self.w), ctypes.c_int(self.h))

    def quad(self, x, y, w, h, argb, z, mode=OP, tex=None, uv=(0, 0, 1, 1), bilinear=False):
        u0, v0, u1, v1 = uv
        q = np.zeros(6, RV)
        pts = [(x, y, u0, v0), (x + w, y, u1, v0), (x, y + h, u0, v1), (x + w, y, u1, v0), (x + w, y + h, u1, v1), (x, y + h, u0, v1)]
        for i, (px, py, u, v) in enumerate(pts):
            q[i] = (px, py, z, u, v, argb)
        self.tris(q, tex, bilinear, mode, False)

    def fog(self, on, rgb=0, start=0.0, end=1.0):
        """slice 3D-6: PVR table fog for the following tris() (W between start and end, towards rgb)"""
        _lib().raster_fog(ctypes.c_int(1 if on else 0), ctypes.c_float((rgb >> 16) & 255), ctypes.c_float((rgb >> 8) & 255),
                          ctypes.c_float(rgb & 255), ctypes.c_float(start), ctypes.c_float(end))

    def image(self):
        return Image.fromarray(np.clip(self.color + 0.5, 0, 255).astype(np.uint8))

    def save(self, path, **kw):
        self.image().save(path, **kw)


def strips_to_tris(strip_rv):
    """A strip (list/array of RV) -> triangle list RV array with consistent winding."""
    out = []
    for j in range(len(strip_rv) - 2):
        a, b, c = strip_rv[j], strip_rv[j + 1], strip_rv[j + 2]
        out.extend((b, a, c) if j & 1 else (a, b, c))
    return np.array(out, RV) if out else np.zeros(0, RV)


# ---------------------------------------------------------------------------- camera
def look_at(eye, target, up=(0, 1, 0)):
    eye, target, up = (np.asarray(v, float) for v in (eye, target, up))
    f = target - eye; f /= np.linalg.norm(f)
    s = np.cross(f, up); s /= np.linalg.norm(s)
    u = np.cross(s, f)
    M = np.eye(4)
    M[0, :3], M[1, :3], M[2, :3] = s, u, -f
    M[:3, 3] = -M[:3, :3] @ eye
    return M


class Camera:
    def __init__(self, eye, target, fov_y=40.0, w=640, h=480):
        self.view = look_at(eye, target)
        self.cx, self.cy = w / 2, h / 2
        self.focal = (h / 2) / np.tan(np.radians(fov_y) / 2)
        self.near_w = 0.5

    def project(self, p):
        """(n,3) world points -> (n,3) screen x, y, z=1/W and the W values."""
        p = np.asarray(p, float)
        v = np.c_[p, np.ones(len(p))] @ self.view.T
        W = -v[:, 2]
        Wc = np.maximum(W, 1e-6)
        x = self.cx + self.focal * v[:, 0] / Wc
        y = self.cy - self.focal * v[:, 1] / Wc
        return np.stack([x, y, 1.0 / Wc], 1), W


def shade(argb, lit, tint=(1, 1, 1, 1)):
    """Same as render3d.cpp shade(): rgb * lit * tint, alpha * tint, clamped, truncated."""
    argb = np.asarray(argb, np.uint64)
    r = np.minimum(((argb >> 16) & 255) * lit * tint[1], 255).astype(np.uint32)
    g = np.minimum(((argb >> 8) & 255) * lit * tint[2], 255).astype(np.uint32)
    b = np.minimum((argb & 255) * lit * tint[3], 255).astype(np.uint32)
    a = np.minimum((argb >> 24) * tint[0], 255).astype(np.uint32)
    return (a << 24) | (r << 16) | (g << 8) | b


DEFAULT_LIGHT = np.array([-0.35, 0.85, 0.40]) / np.linalg.norm([-0.35, 0.85, 0.40])
