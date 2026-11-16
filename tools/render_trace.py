#!/usr/bin/env python3
"""
Approximate PVR rasterizer for host traces -> PNG preview (no Dreamcast needed).
Model (mirrors platform/kos/main.cpp + engine.h draw-order contract):
  * L lines give the alpha flag per texture: alpha=0 -> OP list (RGB565, opaque quad),
    alpha=1 -> TR list (ARGB1555: texel alpha >= 128 is opaque, tinted/faded by the vertex argb).
  * Sprite z = explicit z if > 0, else 1 + seq*1e-4 (seq = sprite call order in the frame).
  * OP: painter's order by z.  TR: sorted by z (autosort), alpha-blended, depth-tested vs OP.
  * The input overlay drawn by main.cpp (not in the trace) is added at z 9.5.
usage: render_trace.py <trace> <frame> <romdisk_dir> <out.png> [--overlay idle|none]
Older traces (short S lines, no L alpha flag) render as opaque sprites.
"""
import sys, math
import numpy as np
from PIL import Image

trace, frame, romdisk, out = sys.argv[1:5]
overlay = sys.argv[sys.argv.index("--overlay") + 1] if "--overlay" in sys.argv else "idle"

alpha_of, calls, cur = {}, [], None
for line in open(trace):
    t = line.split()
    if not t: continue
    if t[0] == "L": alpha_of.setdefault(t[1], len(t) > 2 and t[2] == "1")
    elif t[0] == "F": cur = t[1]
    elif cur == frame and t[0] in ("S", "R"): calls.append(t)
if not calls: sys.exit(f"frame {frame} not found/empty")

W, H = 640, 480
fb = np.zeros((H, W, 3), np.float32)
depth = np.zeros((H, W), np.float32)
tex_cache = {}

def texture(path):
    if path not in tex_cache:
        tex_cache[path] = Image.open(romdisk + "/" + path.replace("/rd/", "")).convert("RGBA")
    return tex_cache[path]

def sprite_layer(t):
    path = t[1]; x, y, w, h, a = map(float, t[2:7]); filt = int(t[7])
    u0, v0, u1, v1 = map(float, t[8:12]) if len(t) >= 12 else (0, 0, 1, 1)
    argb = int(t[12], 16) if len(t) >= 13 else 0xFFFFFFFF
    tex = texture(path); tw, th = tex.size
    src = tex.crop((round(u0 * tw), round(v0 * th), round(u1 * tw), round(v1 * th)))
    q = src.resize((max(1, round(w)), max(1, round(h))), Image.BILINEAR if filt == 2 else Image.NEAREST)
    qa = np.asarray(q, np.float32).copy()
    if alpha_of.get(path, False):
        qa[..., 3] = np.where(qa[..., 3] >= 128, 255.0, 0.0)                 # ARGB1555 alpha bit
        qa[..., :3] *= np.array([(argb >> 16) & 255, (argb >> 8) & 255, argb & 255], np.float32) / 255.0
        qa[..., 3] *= ((argb >> 24) & 255) / 255.0
    else:
        qa[..., 3] = 255.0                                                       # RGB565: whole quad opaque
    im = Image.fromarray(np.clip(qa, 0, 255).astype(np.uint8), "RGBA")
    if abs(a) > 1e-6: im = im.rotate(-math.degrees(a), resample=Image.NEAREST, expand=True)
    return np.asarray(im, np.float32), x, y

def blit(layer, x, y, z, opaque_pass):
    qh, qw = layer.shape[:2]; x0, y0 = round(x - qw / 2), round(y - qh / 2)
    xs, ys, xe, ye = max(0, x0), max(0, y0), min(W, x0 + qw), min(H, y0 + qh)
    if xs >= xe or ys >= ye: return
    sub = layer[ys - y0:ye - y0, xs - x0:xe - x0]
    a = sub[..., 3:4] / 255.0
    region, dreg = fb[ys:ye, xs:xe], depth[ys:ye, xs:xe]
    if opaque_pass:
        m = (sub[..., 3] > 0) & (z >= dreg)
        region[m] = sub[..., :3][m]; dreg[m] = z
    else:
        vis = (z >= dreg)[..., None]
        region[:] = np.where(vis, region * (1 - a) + sub[..., :3] * a, region)

def rect_layer(w, h, argb):
    w, h = max(1, int(round(w))), max(1, int(round(h)))
    layer = np.zeros((h, w, 4), np.float32)
    layer[...] = [(argb >> 16) & 255, (argb >> 8) & 255, argb & 255, (argb >> 24) & 255]
    return layer

op, tr, seq = [], [], 0
for t in calls:
    if t[0] == "S":
        zexp = float(t[13]) if len(t) >= 14 else 0.0
        z = zexp if zexp > 0 else 1.0 + seq * 1e-4
        seq += 1
        (tr if alpha_of.get(t[1], False) else op).append((z, "S", t))
    else:
        x, y, w, h = map(float, t[2:6]); argb = int(t[6], 16); z = float(t[7])
        (op if t[1] == "OP" else tr).append((z, "R", (x, y, w, h, argb)))
if overlay == "idle":   # main.cpp debug overlay, pad untouched (correct polarity: all dark)
    for r in [(20, 20, 20, 20, 0xFF202020), (20, 45, 20, 20, 0xFF202020), (45, 45, 20, 20, 0xFF202020),
              (70, 45, 20, 20, 0xFF202020), (117, 42, 6, 6, 0xFFFF0000)]:
        tr.append((9.5, "R", r))

for items, opaque in ((op, True), (tr, False)):
    for z, kind, t in sorted(items, key=lambda i: i[0]):
        if kind == "S":
            layer, x, y = sprite_layer(t); blit(layer, x, y, z, opaque)
        else:
            x, y, w, h, argb = t; layer = rect_layer(w, h, argb)
            blit(layer, x + layer.shape[1] / 2, y + layer.shape[0] / 2, z, opaque)

Image.fromarray(np.clip(fb, 0, 255).astype(np.uint8)).save(out)
print(f"rendered frame {frame}: {len(op)} OP, {len(tr)} TR items -> {out}")
