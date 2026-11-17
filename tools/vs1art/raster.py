"""VENDORED from the VS1 lineage (larp3-2/tools/dmf/raster.py), unchanged below this note (vs1_fbx2dmf.py's
hidden-surface pass)."""
"""Tiny numpy z-buffer rasterizer used by the converter (visibility pass) and dmf_preview.py.

Conventions match the PVR path in src/render3d.cpp: screen x right, y down, depth = 1/w,
larger depth = nearer (PVR_DEPTHCMP_GREATER). Attributes are interpolated perspective-correctly.
"""
import numpy as np


def raster(sx, sy, invw, tris, width, height, attrs=None, depth=None, ids=None, cull=None):
    """Rasterizes triangles into depth (and optionally an id buffer / attribute buffer).

    sx, sy, invw : (n,) screen position and 1/w per vertex
    tris         : (t, 3) vertex indices
    attrs        : optional (n, k) per-vertex attributes -> returns (h, w, k) buffer
    ids          : optional (t,) triangle ids written to an int buffer (-1 = empty)
    cull         : None | 'cw' | 'ccw' - drop triangles with that screen winding (y down)
    """
    if depth is None:
        depth = np.zeros((height, width), np.float32)
    idbuf = np.full((height, width), -1, np.int32) if ids is not None else None
    out = np.zeros((height, width, attrs.shape[1]), np.float32) if attrs is not None else None
    for t, (a, b, c) in enumerate(tris):
        if invw[a] <= 0 or invw[b] <= 0 or invw[c] <= 0:
            continue
        x0, y0, x1, y1, x2, y2 = sx[a], sy[a], sx[b], sy[b], sx[c], sy[c]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)   # > 0: clockwise on a y-down screen
        if area == 0:
            continue
        if cull == "ccw" and area < 0:
            continue
        if cull == "cw" and area > 0:
            continue
        minx = max(int(np.floor(min(x0, x1, x2))), 0)
        maxx = min(int(np.ceil(max(x0, x1, x2))), width - 1)
        miny = max(int(np.floor(min(y0, y1, y2))), 0)
        maxy = min(int(np.ceil(max(y0, y1, y2))), height - 1)
        if minx > maxx or miny > maxy:
            continue
        ys, xs = np.mgrid[miny:maxy + 1, minx:maxx + 1]
        px, py = xs + 0.5, ys + 0.5
        w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area
        w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        z = w0 * invw[a] + w1 * invw[b] + w2 * invw[c]
        region = depth[miny:maxy + 1, minx:maxx + 1]
        win = inside & (z > region)
        if not win.any():
            continue
        region[win] = z[win]
        if idbuf is not None:
            idbuf[miny:maxy + 1, minx:maxx + 1][win] = ids[t]
        if out is not None:
            # perspective-correct: interpolate attr/w, divide by interpolated 1/w
            pa = (w0[win, None] * attrs[a] * invw[a] + w1[win, None] * attrs[b] * invw[b] +
                  w2[win, None] * attrs[c] * invw[c]) / z[win, None]
            out[miny:maxy + 1, minx:maxx + 1][win] = pa
    return depth, idbuf, out


def look_at(eye, target, up=(0.0, 1.0, 0.0)):
    """Right-handed view matrix (camera looks down -Z)."""
    eye, target, up = (np.asarray(v, np.float64) for v in (eye, target, up))
    f = target - eye
    f /= np.linalg.norm(f)
    s = np.cross(f, up)
    if np.linalg.norm(s) < 1e-6:
        s = np.cross(f, (0.0, 0.0, 1.0))
    s /= np.linalg.norm(s)
    u = np.cross(s, f)
    m = np.eye(4)
    m[0, :3], m[1, :3], m[2, :3] = s, u, -f
    m[:3, 3] = -m[:3, :3] @ eye
    return m


def perspective(fovy_deg, aspect, near):
    """Infinite-far perspective whose clip w = view-space depth (so 1/w is PVR depth)."""
    f = 1.0 / np.tan(np.radians(fovy_deg) / 2)
    m = np.zeros((4, 4))
    m[0, 0], m[1, 1] = f / aspect, f
    m[2, 2], m[2, 3] = -1.0, -2.0 * near
    m[3, 2] = -1.0
    return m


def project(mvp, pos, width, height):
    """Model-space positions (n,3) -> screen x, y (y down) and 1/w."""
    p = np.c_[pos, np.ones(len(pos))] @ mvp.T
    w = p[:, 3]
    invw = np.where(w > 1e-6, 1.0 / np.maximum(w, 1e-6), -1.0)
    sx = (p[:, 0] * invw * 0.5 + 0.5) * width
    sy = (0.5 - p[:, 1] * invw * 0.5) * height
    return sx, sy, invw


def visible_ids(sx, sy, depth_v, tris, width, height, tol=5e-3):
    """Vectorised visibility: indices of triangles that own a visible pixel in this view.

    depth_v is a per-vertex depth that is linear in screen space (orthographic views), larger =
    nearer. All fragments of all triangles are generated at once (triangles bucketed by
    bounding-box size) and the per-pixel max depth is resolved by sorting. A triangle is visible if
      * one of its fragments is within `tol` (model units) of the pixel's front depth, or
      * its centroid is (catches sub-pixel triangles that cover no pixel centre).
    Both tests are conservative: ties and near-ties keep the triangle."""
    f32 = np.float32
    sx, sy, dv = sx.astype(f32), sy.astype(f32), depth_v.astype(f32)
    tris = np.asarray(tris)
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    x0, y0, x1, y1, x2, y2 = sx[a], sy[a], sx[b], sy[b], sx[c], sy[c]
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    ok = area != 0
    minx = np.floor(np.minimum(np.minimum(x0, x1), x2)).astype(np.int32)
    miny = np.floor(np.minimum(np.minimum(y0, y1), y2)).astype(np.int32)
    ext = np.ceil(np.maximum(np.maximum(x0, x1), x2)).astype(np.int32) - minx + 1
    ext = np.maximum(ext, np.ceil(np.maximum(np.maximum(y0, y1), y2)).astype(np.int32) - miny + 1)
    frag_pix, frag_z, frag_t = [], [], []
    # large triangles one by one over their exact (w x h) rectangle: square size buckets would
    # materialise huge grids for long thin triangles (roof strips, trims) and swap
    wext = np.ceil(np.maximum(np.maximum(x0, x1), x2)).astype(np.int32) - minx + 1
    hext = np.ceil(np.maximum(np.maximum(y0, y1), y2)).astype(np.int32) - miny + 1
    for t in np.nonzero(ok & (ext > 32))[0]:
        bx0, by0 = max(int(minx[t]), 0), max(int(miny[t]), 0)
        bx1, by1 = min(int(minx[t] + wext[t]), width), min(int(miny[t] + hext[t]), height)
        if bx0 >= bx1 or by0 >= by1:
            continue
        oy, ox = np.mgrid[by0:by1, bx0:bx1].astype(f32)
        px, py = ox + f32(0.5), oy + f32(0.5)
        A = area[t]
        w0 = ((x1[t] - px) * (y2[t] - py) - (x2[t] - px) * (y1[t] - py)) / A
        w1 = ((x2[t] - px) * (y0[t] - py) - (x0[t] - px) * (y2[t] - py)) / A
        w2 = f32(1.0) - w0 - w1
        mm = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        yy, xx = np.nonzero(mm)
        frag_pix.append((yy + by0).astype(np.int64) * width + (xx + bx0))
        frag_z.append(w0[yy, xx] * dv[a[t]] + w1[yy, xx] * dv[b[t]] + w2[yy, xx] * dv[c[t]])
        frag_t.append(np.full(len(yy), t, np.int64))
    lo = 0
    for size in (2, 4, 8, 16, 32):
        sel = np.nonzero(ok & (ext > lo) & (ext <= size))[0]
        lo = size
        if sel.size == 0:
            continue
        oy, ox = np.mgrid[0:size, 0:size].astype(f32)
        px = minx[sel, None, None].astype(f32) + ox[None] + f32(0.5)
        py = miny[sel, None, None].astype(f32) + oy[None] + f32(0.5)
        X0, Y0, X1, Y1, X2, Y2, A = (v[sel, None, None] for v in (x0, y0, x1, y1, x2, y2, area))
        w0 = ((X1 - px) * (Y2 - py) - (X2 - px) * (Y1 - py)) / A
        w1 = ((X2 - px) * (Y0 - py) - (X0 - px) * (Y2 - py)) / A
        w2 = f32(1.0) - w0 - w1
        m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0) & (px >= 0) & (py >= 0) & (px < width) & (py < height)
        ti, yy, xx = np.nonzero(m)
        z = w0[ti, yy, xx] * dv[a[sel[ti]]] + w1[ti, yy, xx] * dv[b[sel[ti]]] + w2[ti, yy, xx] * dv[c[sel[ti]]]
        frag_pix.append((miny[sel[ti]] + yy).astype(np.int64) * width + (minx[sel[ti]] + xx))
        frag_z.append(z)
        frag_t.append(sel[ti])
    depth = np.full(width * height, -np.inf, f32)
    vis = np.zeros(len(tris), bool)
    if frag_pix:
        pix, z, t = np.concatenate(frag_pix), np.concatenate(frag_z), np.concatenate(frag_t)
        order = np.lexsort((z, pix))                     # by pixel, then depth ascending
        last = np.r_[pix[order][1:] != pix[order][:-1], True]
        depth[pix[order][last]] = z[order][last]         # front-most depth per pixel
        vis[t[z >= depth[pix] - tol]] = True
    # centroid test for everything (sub-pixel triangles)
    cx, cy = (x0 + x1 + x2) / 3, (y0 + y1 + y2) / 3
    cz = (dv[a] + dv[b] + dv[c]) / 3
    ix, iy = np.floor(cx).astype(np.int64), np.floor(cy).astype(np.int64)
    inb = (ix >= 0) & (iy >= 0) & (ix < width) & (iy < height)
    front = np.full(len(tris), -np.inf, f32)
    front[inb] = depth[iy[inb] * width + ix[inb]]
    vis |= inb & ((cz >= front - tol) | ~np.isfinite(front))
    return np.nonzero(vis)[0]
