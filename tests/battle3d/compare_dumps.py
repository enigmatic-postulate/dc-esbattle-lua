"""compare_dumps.py - the frames dumped into W/vc_a_* (romdisk A) and W/vc_b_* (romdisk B) must
render to the same pixels (tools/render_frame.py), within 2 colour levels per channel: restripping
changes the order of a triangle's corners, which moves the interpolation's float rounding by one
level (a culled triangle that was visible shows as a different surface: tens of levels).
    python3 tests/battle3d/compare_dumps.py W RD_A RD_B"""
TOL = 2
import glob, os, sys
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import render_frame
W, ra, rb = sys.argv[1], sys.argv[2], sys.argv[3]
tot = bad = worst = 0
for d1 in sorted(glob.glob(os.path.join(W, 'vc_a_*'))):
    d2 = d1.replace('vc_a_', 'vc_b_')
    for f in sorted(glob.glob(os.path.join(d1, 'frame_*.txt'))):
        a = np.asarray(render_frame.render(f, ra)).astype(int)
        b = np.asarray(render_frame.render(f.replace(d1, d2), rb)).astype(int)
        n = int((np.abs(a - b).max(-1) > TOL).sum())
        tot += 1; bad += n > 0; worst = max(worst, n)
        if n: print('  %s %s: %d pixels differ' % (os.path.basename(d1), os.path.basename(f), n))
msg = 'view-culled props: %d frames of %d battles identical to the full props (within %d colour levels)' % (tot, len(glob.glob(os.path.join(W, 'vc_a_*'))), TOL) if not bad else \
      'view culling: %d of %d frames differ (worst %d pixels)' % (bad, tot, worst)
print(msg)
sys.exit(1 if bad or tot == 0 else 0)
