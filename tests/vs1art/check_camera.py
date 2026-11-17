#!/usr/bin/env python3
"""check_camera.py - the field camera is VS1's (field3d.cpp): the eye 9 up and 9 back from the look-at point
(pitch 45, 12.73 units), the look-at point 0.55 above the ground and clamped to the map (4.5 from the sides,
7 from the north edge, 3 from the south edge), on every field frame of a traced route (V lines, --view), except
while a dialog's two-shot eases back (T4: 20 ticks).

    python3 tests/vs1art/check_camera.py TRACE W H
"""
import sys


def main():
    path, w, h = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    n = ok = eased = 0
    bad = []
    last_dialog = -10 ** 9
    tick = 0
    for ln in open(path):
        if ln.startswith('F ') and ln.split()[1].isdigit():
            tick = int(ln.split()[1])
        if not ln.startswith('V '):
            continue
        d = dict(kv.split('=', 1) for kv in ln.split()[1:] if '=' in kv)
        if d['mode'] == 'dialog':
            last_dialog = tick
            continue
        if d['mode'] != 'field':
            continue
        n += 1
        cx, cz = map(float, d['cam'].split(','))
        ex, ey, ez = map(float, d['eye'].split(','))
        inside = 4.5 - 1e-3 <= cx <= w - 4.5 + 1e-3 and 7.0 - 1e-3 <= cz <= h - 3.0 + 1e-3
        match = abs(ex - cx) < 0.011 and abs(ey - 9.55) < 0.011 and abs(ez - (cz + 9.0)) < 0.011
        if match and inside:
            ok += 1
        elif tick - last_dialog <= 20 and inside:
            eased += 1
        else:
            bad.append('tick %d: look (%.2f, %.2f) eye (%.2f, %.2f, %.2f)' % (tick, cx, cz, ex, ey, ez))
    if bad or n == 0:
        print('%d of %d field frames off VS1\'s camera:\n%s' % (len(bad), n, '\n'.join(bad[:8])))
        return 1
    print('field camera == VS1\'s on %d of %d field frames (eye = look + (0, 9, 9), look 0.55 up, clamped to the map); '
          'the other %d are T4 eases back from a two-shot' % (ok, n, eased))
    return 0


if __name__ == '__main__':
    sys.exit(main())
