"""preview.py - render .dmf models (posed with .dma clips, weapons attached) with the reference
math (dmfio) and the PVR-like rasterizer (raster). Host tool for QC sheets and docs.

    python3 tools/battle3d/preview.py romdisk/models/skel_warrior.dmf out.png --clip skel_death --frames 6 \
        --dma romdisk/anims/rig_medium.dma --weapon handslot.r=romdisk/models/w_blade.dmf
"""
import argparse
import os
import sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmfio  # noqa: E402
import raster  # noqa: E402

ROMDISK = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'romdisk')


def load_tex(path_or_model, alpha=False):
    if isinstance(path_or_model, dict):
        return dmfio.texture_rgba(path_or_model['texture']) if path_or_model['texture'] else None
    p = path_or_model
    if p.startswith('/rd/'):
        p = os.path.join(ROMDISK, p[4:])
    im = np.array(Image.open(p).convert('RGBA'))
    if alpha:
        im[..., 3] = np.where(im[..., 3] >= 128, 255, 0)
    else:
        im[..., 3] = 255
    return im


def model_textures(m, holder_tex=None):
    """Texture (RGBA) per material; holder_tex replaces '@' (weapons drawn with the holder's atlas)."""
    emb = dmfio.texture_rgba(m['texture']) if m['texture'] else None
    out = []
    mats = m['materials'] or [dict(tex='@' if m['texture'] else '', flags=(dmfio.MAT_BILINEAR if m['texture'] and m['texture']['filter'] else 0))]
    for mt in mats:
        if mt['tex'] == '':
            out.append((None, mt))
        elif mt['tex'] == '@':
            out.append((holder_tex if holder_tex is not None else emb, mt))
        else:
            out.append((load_tex(mt['tex'], bool(mt['flags'] & dmfio.MAT_ALPHA)), mt))
    return out


def draw_model(canvas, cam, m, world, skin=None, light=raster.DEFAULT_LIGHT, ambient=0.55, diffuse=0.5, tint=(1, 1, 1, 1),
               holder_tex=None, cull=True, mode=None, colors=None):
    """world: 4x4 model->world. skin: list of 4x4 skin matrices (or None = bind/static)."""
    if skin is not None and m['bones']:
        P, N = dmfio.skinned_positions(m, skin)
    else:
        v = m['verts']
        P = np.stack([v['x'], v['y'], v['z']], 1).astype(float)
        N = np.stack([v['nx'], v['ny'], v['nz']], 1) / 127.0
    Pw = (np.c_[P, np.ones(len(P))] @ world.T)[:, :3]
    Nw = N @ world[:3, :3].T
    Nw /= np.maximum(np.linalg.norm(Nw, axis=1, keepdims=True), 1e-9)
    if colors is None:
        ndl = Nw @ light
        lit = np.minimum(ambient + np.where(ndl > 0, diffuse * ndl, 0), 1.0)
        colors = raster.shade(m['verts']['argb'], lit, tint)
    S, W = cam.project(Pw)
    rv = np.zeros(len(P), raster.RV)
    rv['x'], rv['y'], rv['z'] = S[:, 0], S[:, 1], S[:, 2]
    rv['u'], rv['v'], rv['argb'] = m['verts']['u'], m['verts']['v'], colors
    texs = model_textures(m, holder_tex)
    for k, bt in enumerate(m['batches']):
        mi = bt['flags'] >> 8
        tex, mt = texs[mi]
        idx = []
        for strip in dmfio.strips_of_batch(m, k):
            idx.extend(dmfio.strip_triangles(strip))
        if not idx:
            continue
        idx = np.array(idx)
        keep = (W[idx] > cam.near_w).all(1)           # previews: drop near-clipped triangles
        tri = rv[idx[keep].reshape(-1)]
        md = mode if mode is not None else (raster.BLEND if mt['flags'] & dmfio.MAT_ALPHA else raster.OP)
        canvas.tris(tri, tex, bool(mt['flags'] & dmfio.MAT_BILINEAR), md,
                    cull and not (bt['flags'] & dmfio.BATCH_DOUBLE_SIDED))


def trs_y(pos, yaw, scale=1.0):
    c, s = np.cos(yaw), np.sin(yaw)
    M = np.eye(4)
    M[:3, :3] = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]]) * scale
    M[:3, 3] = pos
    return M


def bone_world(m, qs, ts, name):
    W = dmfio.world_mats(m, qs, ts)
    return W[[b['name'] for b in m['bones']].index(name)]


def label(img, text, xy=(4, 2), fill=(255, 255, 255)):
    d = ImageDraw.Draw(img)
    d.text(xy, text, fill=fill)
    return img


def pose_for(m, lib, clip_name, t):
    if lib is None or clip_name in (None, 'bind'):
        return dmfio.bind_pose(m)
    clip = [c for c in lib['clips'] if c['name'] == clip_name][0]
    remap = [lib['bones'].index(b['name']) if b['name'] in lib['bones'] else -1 for b in m['bones']]
    return dmfio.sample_clip(m, clip, t, remap)


def render_pose(m, qs, ts, weapons=(), yaw=0.6, size=(320, 320), eye=None, target=(0, 1.0, 0), fov=40, clear=0x2A2F3A,
                scale=1.0, pos=(0, 0, 0)):
    c = raster.Canvas(size[0], size[1], clear)
    cam = raster.Camera(eye if eye is not None else (0, 1.6, 5.6), target, fov, size[0], size[1])
    world = trs_y(pos, yaw, scale)
    skin = dmfio.skin_mats(m, qs, ts)
    draw_model(c, cam, m, world, skin)
    tex = load_tex(m)
    for slot, wm, off in weapons:
        Wb = bone_world(m, qs, ts, slot)
        draw_model(c, cam, wm, world @ Wb @ off, None, holder_tex=tex)
    return c.image()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dmf'); ap.add_argument('out')
    ap.add_argument('--dma'); ap.add_argument('--clip', default='bind'); ap.add_argument('--frames', type=int, default=1)
    ap.add_argument('--weapon', action='append', default=[], help='slot=path.dmf')
    ap.add_argument('--yaw', type=float, default=0.6)
    a = ap.parse_args()
    m = dmfio.read_dmf(a.dmf)
    lib = dmfio.read_dma(a.dma) if a.dma else None
    weapons = []
    for w in a.weapon:
        slot, p = w.split('=')
        weapons.append((slot, dmfio.read_dmf(p), np.eye(4)))
    dur = 1.0
    if lib is not None and a.clip != 'bind':
        c = [c for c in lib['clips'] if c['name'] == a.clip][0]
        dur = (c['frames'] - (0 if c['loop'] else 1)) / c['fps']
    ims = []
    for i in range(a.frames):
        t = dur * i / max(1, a.frames - (0 if a.frames == 1 else 1))
        qs, ts = pose_for(m, lib, a.clip, t)
        ims.append(label(render_pose(m, qs, ts, weapons, a.yaw), '%s t=%.2f' % (a.clip, t)))
    sheet = Image.new('RGB', (320 * len(ims), 320))
    for i, im in enumerate(ims):
        sheet.paste(im, (320 * i, 0))
    sheet.save(a.out)


if __name__ == '__main__':
    main()
