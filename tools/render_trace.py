#!/usr/bin/env python3
"""
Approximate PVR rasterizer for harness traces -> PNG preview (no Dreamcast needed).
Models: 640x480 black framebuffer; textures loaded OPAQUE (PNG_NO_ALPHA, as the engine does
today); OP list = sprites in KOS flush order (per texture, nearest batch then bilinear batch)
at z=1.0; TR list = rects alpha-blended, farther z first, depth-tested against OP (larger z =
nearer); plus main.cpp's input overlay (not part of the trace).
usage: render_trace.py <trace> <frame> <romdisk_dir> <out.png> [--pad-polarity inverted|fixed]
"""
import sys, math
import numpy as np
from PIL import Image

trace, frame, romdisk, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
polarity = sys.argv[sys.argv.index("--pad-polarity") + 1] if "--pad-polarity" in sys.argv else "inverted"

load_order, calls, cur = [], [], None
for line in open(trace):
    t = line.split()
    if not t: continue
    if t[0] == "L" and t[1] not in load_order: load_order.append(t[1])
    elif t[0] == "F": cur = t[1]
    elif cur == frame and t[0] in ("S", "R"): calls.append(t)
if not calls: sys.exit(f"frame {frame} not found/empty")

tex_cache = {}
def texture(path):
    if path not in tex_cache:
        im = Image.open(romdisk + "/" + path.replace("/rd/", "")).convert("RGBA")
        rgb = Image.new("RGB", im.size); rgb.paste(im.convert("RGB"))   # alpha dropped, like PNG_NO_ALPHA
        tex_cache[path] = rgb
    return tex_cache[path]

W, H = 640, 480
fb = np.zeros((H, W, 3), np.float32)
depth = np.zeros((H, W), np.float32)

def draw_sprite(t):
    path = t[1]; x, y, w, h, a = map(float, t[2:7]); filt = int(t[7])
    u0, v0, u1, v1 = map(float, t[8:12]) if len(t) >= 12 else (0, 0, 1, 1)
    tex = texture(path); tw, th = tex.size
    src = tex.crop((round(u0 * tw), round(v0 * th), round(u1 * tw), round(v1 * th)))
    iw, ih = max(1, round(w)), max(1, round(h))
    q = src.resize((iw, ih), Image.BILINEAR if filt == 2 else Image.NEAREST).convert("RGBA")
    if abs(a) > 1e-6: q = q.rotate(-math.degrees(a), resample=Image.NEAREST, expand=True)
    qa = np.asarray(q, np.float32)
    qh, qw = qa.shape[:2]; x0, y0 = round(x - qw / 2), round(y - qh / 2)
    xs, ys = max(0, x0), max(0, y0); xe, ye = min(W, x0 + qw), min(H, y0 + qh)
    if xs >= xe or ys >= ye: return
    sub = qa[ys - y0:ye - y0, xs - x0:xe - x0]
    m = sub[..., 3] > 0                      # quad coverage (rotation padding excluded)
    fb[ys:ye, xs:xe][m] = sub[..., :3][m]
    depth[ys:ye, xs:xe][m] = np.maximum(depth[ys:ye, xs:xe][m], 1.0)

def draw_rect(x, y, w, h, argb, z):
    a = ((argb >> 24) & 255) / 255.0; col = np.array([(argb >> 16) & 255, (argb >> 8) & 255, argb & 255], np.float32)
    xs, ys = max(0, int(round(x))), max(0, int(round(y))); xe, ye = min(W, int(round(x + w))), min(H, int(round(y + h)))
    if xs >= xe or ys >= ye: return
    region = fb[ys:ye, xs:xe]; vis = depth[ys:ye, xs:xe] <= z
    region[vis] = region[vis] * (1 - a) + col * a

# OP list: KOS flush order = per texture (load order), nearest pass then bilinear pass
sprites = [c for c in calls if c[0] == "S"]
key = lambda p: load_order.index(p) if p in load_order else 99
for filt in ("0", "2"):
    for path in sorted({c[1] for c in sprites}, key=key):
        for c in sprites:
            if c[1] == path and c[7] == filt: draw_sprite(c)

# TR list: main.cpp input overlay (pad idle) + traced rects, farther (smaller z) first
green, dark = 0xFF00FF00, 0xFF202020
pad = green if polarity == "inverted" else dark        # idle pad reads "pressed" with the shipped polarity bug
tr = [(20, 20, 20, 20, pad, 0.01), (20, 45, 20, 20, pad, 0.01), (45, 45, 20, 20, pad, 0.01), (70, 45, 20, 20, pad, 0.01),
      (120 - 3 + 3, 45, 6, 6, 0xFFFF0000, 0.01), (317, 197, 6, 6, 0xFF00FF00, 0.01), (317, 277, 6, 6, 0xFFFF0000, 0.01)]
tr += [(float(c[2]), float(c[3]), float(c[4]), float(c[5]), int(c[6], 16), float(c[7])) for c in calls if c[0] == "R" and c[1] == "TR"]
for r in sorted(tr, key=lambda r: r[5]): draw_rect(*r)

Image.fromarray(np.clip(fb, 0, 255).astype(np.uint8)).save(out)
print(f"rendered frame {frame}: {len(sprites)} sprites, {len(tr)} TR rects -> {out}")
