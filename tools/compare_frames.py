#!/usr/bin/env python3
"""Compares the 3D vertex output of two builds, frame by frame.

    python3 tools/compare_frames.py REF_DIR TEST_DIR

REF_DIR and TEST_DIR hold frame_NNNNN.txt dumps of the same input script
(battle_trace --dump DIR --dump-frames ...). Batches are matched in submission order;
when a batch's vertex count differs, its strips are matched by geometry and strips that
exist on one side only are reported with their screen bounds and rasterised against the
640x480 pixel centres, so a culling difference can be checked for visibility).

Reports, over every matched vertex: screen position error (pixels, for vertices within 64 px of
the screen; vertices far off screen, next to the near plane, get a relative error), depth (1/W)
relative error, UV error and per-channel colour difference.
"""
import glob
import os
import sys


def load(path):
    batches, cur = [], None
    for line in open(path):
        t = line.split()
        if not t:
            continue
        if t[0] == 'b':
            cur = (int(t[1]), [])
            batches.append(cur)
        elif t[0] in ('v', 'e') and cur is not None:
            cur[1].append((t[0], float(t[1]), float(t[2]), float(t[3]), float(t[4]), float(t[5]), int(t[6], 16)))
    return batches


def strips(verts):
    out, cur = [], []
    for v in verts:
        cur.append(v)
        if v[0] == 'e':
            out.append(cur)
            cur = []
    return out


def pixels_covered(st):
    """Pixel centres of the 640x480 screen covered by the strip's triangles."""
    n = 0
    for k in range(len(st) - 2):
        (x0, y0), (x1, y1), (x2, y2) = [(v[1], v[2]) for v in st[k:k + 3]]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if area == 0:
            continue
        lo_x, hi_x = max(0, int(min(x0, x1, x2))), min(639, int(max(x0, x1, x2)) + 1)
        lo_y, hi_y = max(0, int(min(y0, y1, y2))), min(479, int(max(y0, y1, y2)) + 1)
        for py in range(lo_y, hi_y + 1):
            for px in range(lo_x, hi_x + 1):
                cx, cy = px + 0.5, py + 0.5
                e0 = (x1 - x0) * (cy - y0) - (cx - x0) * (y1 - y0)
                e1 = (x2 - x1) * (cy - y1) - (cx - x1) * (y2 - y1)
                e2 = (x0 - x2) * (cy - y2) - (cx - x2) * (y0 - y2)
                if (e0 >= 0 and e1 >= 0 and e2 >= 0) or (e0 <= 0 and e1 <= 0 and e2 <= 0):
                    n += 1
    return n


def offscreen(st):
    return pixels_covered(st) == 0


def main():
    ref_dir, test_dir = sys.argv[1], sys.argv[2]
    errs_xy, errs_z, errs_uv, errs_c, far_rel = [], [], [], [], []
    frames = 0
    unmatched = []
    for rp in sorted(glob.glob(os.path.join(ref_dir, 'frame_*.txt'))):
        tp = os.path.join(test_dir, os.path.basename(rp))
        if not os.path.exists(tp):
            print('missing', tp)
            continue
        frames += 1
        rb, tb = load(rp), load(tp)
        if len(rb) != len(tb):
            print(f'{os.path.basename(rp)}: batch count {len(rb)} vs {len(tb)}')
            continue
        for (rm, rv), (tm, tv) in zip(rb, tb):
            if rm != tm:
                print(f'{os.path.basename(rp)}: material order differs ({rm} vs {tm})')
                continue
            if len(rv) == len(tv):
                pairs = list(zip(rv, tv))
            else:
                # Strips keep their submission order: align the two lists, letting a strip that is
                # missing on one side (culled) be skipped.
                rs, ts = strips(rv), strips(tv)

                def same(s, u):   # far off-screen vertices (near the near plane) get a relative tolerance
                    return len(s) == len(u) and all(abs(a[1] - b[1]) + abs(a[2] - b[2]) < 4.0 + 2e-3 * (abs(a[1]) + abs(a[2]))
                                                    and abs(a[4] - b[4]) + abs(a[5] - b[5]) < 1e-3 for a, b in zip(s, u))

                pairs, i, j = [], 0, 0
                while i < len(rs) or j < len(ts):
                    if i < len(rs) and j < len(ts) and same(rs[i], ts[j]):
                        pairs += list(zip(rs[i], ts[j]))
                        i += 1
                        j += 1
                        continue
                    # which side has the extra strip? look ahead a few strips on each side
                    skip_ref = next((k for k in range(1, 6) if i + k < len(rs) and j < len(ts) and same(rs[i + k], ts[j])), None)
                    skip_test = next((k for k in range(1, 6) if j + k < len(ts) and i < len(rs) and same(rs[i], ts[j + k])), None)
                    if j >= len(ts) or (skip_ref is not None and (skip_test is None or skip_ref <= skip_test)):
                        n = skip_ref if skip_ref is not None else 1
                        for s in rs[i:i + n]:
                            unmatched.append((os.path.basename(rp), 'ref only', len(s), offscreen(s), s))
                        i += n
                    else:
                        n = skip_test if skip_test is not None else 1
                        for u in ts[j:j + n]:
                            unmatched.append((os.path.basename(rp), 'test only', len(u), offscreen(u), u))
                        j += n
            for a, b in pairs:
                if not (-64.0 <= a[1] <= 704.0 and -64.0 <= a[2] <= 544.0):      # far off screen: relative error
                    far_rel.append(max(abs(a[1] - b[1]), abs(a[2] - b[2])) / max(abs(a[1]), abs(a[2])))
                else:
                    errs_xy.append(max(abs(a[1] - b[1]), abs(a[2] - b[2])))
                errs_z.append(abs(a[3] - b[3]) / max(abs(a[3]), 1e-6))
                errs_uv.append(max(abs(a[4] - b[4]), abs(a[5] - b[5])))
                errs_c.append(max(abs(((a[6] >> s) & 255) - ((b[6] >> s) & 255)) for s in (0, 8, 16, 24)))

    def pct(v, p):
        v = sorted(v)
        return v[min(len(v) - 1, int(p * len(v)))] if v else 0.0

    n = len(errs_z)
    print(f'frames {frames}, matched vertices {n} ({len(errs_xy)} on or near the screen)')
    if n:
        print(f'  screen xy error px : max {max(errs_xy):.4f}  p99.9 {pct(errs_xy, 0.999):.4f}  p99 {pct(errs_xy, 0.99):.4f}  median {pct(errs_xy, 0.5):.5f}'
              '   (vertices within 64 px of the screen)')
        if far_rel:
            print(f'  far off-screen     : {len(far_rel)} vertices, max relative xy error {max(far_rel):.2e}')
        print(f'  depth 1/W rel err  : max {max(errs_z):.2e}  p99 {pct(errs_z, 0.99):.2e}')
        print(f'  uv error           : max {max(errs_uv):.2e}')
        print(f'  colour diff (0-255): max {max(errs_c)}  vertices with any diff {sum(1 for c in errs_c if c)}')
    vis = [u for u in unmatched if not u[3]]
    print(f'  strips on one side only: {len(unmatched)} ({len(unmatched) - len(vis)} cover no pixel, {len(vis)} cover pixels)')
    for u in vis[:10]:
        xs = [v[1] for v in u[4]]
        ys = [v[2] for v in u[4]]
        print(f'    {u[0]} {u[1]}: {u[2]} verts x {min(xs):.1f}..{max(xs):.1f} y {min(ys):.1f}..{max(ys):.1f}, {pixels_covered(u[4])} pixels')


if __name__ == '__main__':
    main()
