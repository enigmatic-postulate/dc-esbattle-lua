"""anim_check.py - C++ anim::sample + dmf::pose_skin == tools/battle3d/dmfio.py, every clip of the
library on a model at 6 times (incl. loop wrap and one-shot clamp). Prints the worst difference.
    python3 tests/battle3d/anim_check.py dump.txt romdisk MODEL [tolerance]"""
import sys, os
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'battle3d'))
import dmfio
dump, rd, model = sys.argv[1], sys.argv[2], sys.argv[3]
tol = float(sys.argv[4]) if len(sys.argv) > 4 else 1e-4
m = dmfio.read_dmf(os.path.join(rd, 'models', model + '.dmf'))
lib = dmfio.read_dma(os.path.join(rd, 'anims', 'rig_medium.dma'))
remap = [lib['bones'].index(b['name']) for b in m['bones']]
clips = {c['name']: c for c in lib['clips']}
worst, n = 0.0, 0
for ln in open(dump):
    p = ln.split()
    name, t = p[0], float(p[1])
    cpp = np.array([float(x) for x in p[2:]]).reshape(len(m['bones']), 3, 4)
    qs, ts = dmfio.sample_clip(m, clips[name], t, remap)
    ref = dmfio.skin_mats(m, qs, ts)
    d = max(np.abs(cpp[i] - ref[i][:3, :]).max() for i in range(len(ref)))
    worst = max(worst, d); n += 1
ok = worst <= tol and n == len(lib['clips']) * 6
print('anim crosscheck %s: %d samples, worst |C++ - Python| = %.2e (tolerance %.0e): %s' % (model, n, worst, tol, 'OK' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
