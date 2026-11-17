#!/usr/bin/env python3
"""check_layout.py - the dialog layout (GDD 7.1, docs/NEXT_SLICE_DIALOG.md 5.2; DoD F4) on host frame dumps
(battle_trace --dump): positions and sizes within 1 px, the box's alpha 0.88, colours and the depth layers.

    python3 tests/dialog/check_layout.py DUMP_DIR TRACE

Frames checked (found from the trace's G lines): a complete page with a portrait (Wren), Rowan's page, a choice,
a sys page. Spec:
  bars      OP rects (0,0,640,36) and (0,444,640,36), black, z 8.0
  box       TR rect (24,318,592,118), alpha 0xE0 (0.88), z 3.0
  portrait  OP rect frame (29,323,108,108) gold E8B848 z 3.1; the portrait sprite 104x104 centred (83,377) z 3.2
  plate     TR rect (144,300,>=96,22) in the speaker's colour, z 3.3; the name's glyphs above it
  text      glyph sprites z 3.5 (their shadows z 3.4): left edge 150, line tops 334 / 356 / 378
  marker    the advance marker centred at x 601, y 421..424 (it bobs 3 px)
  choice    TR rect right edge 616, bottom edge 294 (24 above the box), width >= 236, height 14 + 26 n, z 3.6
  sys       no portrait, no plate; gold (FFCD75) glyphs centred on x 320
  HUD       hidden: no HP panel (500,12), inventory panel (500,42) nor objective plate (16,14)
"""
import os, re, sys

dump_dir, trace = sys.argv[1], sys.argv[2]
G = {}
for ln in open(trace):
    if ln.startswith('G '):
        p = ln.split()
        G[int(p[1])] = dict(kv.split('=', 1) for kv in p[2:])
want = {}
for t in sorted(G):
    g = G[t]
    if g['box'] != '1':
        continue
    if 'wren_page' not in want and g['op'] == 'page' and g['spk'] == '1' and g['complete'] == '1' and g['slide'] == '0' and g['port'] == '1':
        want['wren_page'] = t
    if 'rowan_page' not in want and g['op'] == 'choice' and g['spk'] == '0' and g['port'] == '1' and g['slide'] == '0':
        want['rowan_choice'] = t
    if 'sys' not in want and g['op'] == 'sys' and g['complete'] == '1':
        want['sys'] = t
print_only = len(sys.argv) > 3 and sys.argv[3] == '--ticks'
if print_only:
    print(','.join(str(v) for v in want.values()))
    sys.exit(0)

errs, checked = [], 0


def near(a, b, tol=1.0):
    return abs(a - b) <= tol


def load(t):
    R, S = [], []
    for ln in open(os.path.join(dump_dir, 'frame_%05d.txt' % t)):
        p = ln.split()
        if not p:
            continue
        if p[0] == 'r':
            R.append((p[1], float(p[2]), float(p[3]), float(p[4]), float(p[5]), int(p[6], 16), float(p[7])))
        elif p[0] == 's':
            S.append(dict(tex=int(p[1]), cx=float(p[2]), cy=float(p[3]), w=float(p[4]), h=float(p[5]), argb=int(p[12], 16), z=float(p[13])))
    return R, S


def expect(cond, what, t):
    global checked
    checked += 1
    if not cond:
        errs.append('tick %d: %s' % (t, what))


def common(t, R, S, sys_page=False):
    expect(any(r[0] == 'OP' and near(r[1], 0) and near(r[2], 0) and near(r[3], 640) and near(r[4], 36) and r[5] == 0xFF000000 and near(r[6], 8.0) for r in R), 'top letterbox bar', t)
    expect(any(r[0] == 'OP' and near(r[2], 444) and near(r[4], 36) and near(r[3], 640) and near(r[6], 8.0) for r in R), 'bottom letterbox bar', t)
    expect(any(r[0] == 'TR' and near(r[1], 24) and near(r[2], 318) and near(r[3], 592) and near(r[4], 118) and (r[5] >> 24) == 0xE0 and near(r[6], 3.0, 0.01) for r in R), 'the box (24,318) 592x118 alpha 0xE0 z 3.0', t)
    expect(not any(near(r[1], 500) and near(r[2], 12) for r in R), 'the HP panel is hidden', t)
    expect(not any(near(r[1], 500) and near(r[2], 42) for r in R), 'the inventory panel is hidden', t)
    expect(not any(near(r[1], 16) and near(r[2], 14) for r in R), 'the objective plate is hidden', t)
    glyphs = [s for s in S if near(s['z'], 3.5, 0.001) and not (s['w'] == 12 and s['h'] == 8)]   # the advance marker is no glyph
    shadows = [s for s in S if near(s['z'], 3.4, 0.001)]
    expect(len(glyphs) > 0 and len(shadows) == len(glyphs), 'text glyphs at z 3.5 each with a shadow at z 3.4', t)
    body = [s for s in glyphs if s['cy'] > 330 and s['w'] < 20]
    tops = sorted(set(round(s['cy'] - s['h'] / 2) for s in body if s['h'] == 14))
    expect(set(tops) <= {334, 356, 378} and 334 in tops if not sys_page else all(t2 in (tops) for t2 in tops), 'text lines at y 334 / 356 / 378 (got %s)' % tops, t)
    return body


for name, t in want.items():
    R, S = load(t)
    if name == 'sys':
        body = common(t, R, S, True)
        expect(not any(r[0] == 'OP' and near(r[1], 29) and near(r[2], 323) for r in R), 'a sys page has no portrait frame', t)
        expect(all((s['argb'] & 0xFFFFFF) == 0xFFCD75 for s in body), 'sys text is gold FFCD75', t)
        if body:
            l, r = min(s['cx'] - s['w'] / 2 for s in body), max(s['cx'] + s['w'] / 2 for s in body)
            expect(near((l + r) / 2, 320, 6), 'sys text centred on x 320 (%.0f..%.0f)' % (l, r), t)
        continue
    body = common(t, R, S)
    expect(any(r[0] == 'OP' and near(r[1], 29) and near(r[2], 323) and near(r[3], 108) and near(r[4], 108) and (r[5] & 0xFFFFFF) == 0xE8B848 and near(r[6], 3.1, 0.01) for r in R), 'the gold portrait frame (29,323) 108x108 z 3.1', t)
    expect(any(near(s['cx'], 83) and near(s['cy'], 377) and near(s['w'], 104) and near(s['h'], 104) and near(s['z'], 3.2, 0.01) for s in S), 'the portrait 104x104 centred (83,377) z 3.2', t)
    expect(any(r[0] == 'TR' and near(r[1], 144) and near(r[2], 300) and r[3] >= 95 and near(r[4], 22) and near(r[6], 3.3, 0.01) for r in R), 'the name plate (144,300) >=96x22 z 3.3', t)
    text = [s for s in body if s['cy'] - s['h'] / 2 >= 333]
    if text:
        expect(near(min(s['cx'] - s['w'] / 2 for s in text), 150), 'text starts at x 150 (%.1f)' % min(s['cx'] - s['w'] / 2 for s in text), t)
    if name == 'wren_page':
        expect(any(601 - 7 <= s['cx'] <= 601 + 7 and 420 <= s['cy'] <= 425 for s in S if near(s['z'], 3.5, 0.001) and s['w'] == 12), 'the advance marker at (601, 421..424)', t)
    if name == 'rowan_choice':
        ch = [r for r in R if r[0] == 'TR' and near(r[6], 3.6, 0.01)]
        expect(len(ch) == 1, 'one choice box at z 3.6', t)
        if ch:
            x, y, w, h = ch[0][1:5]
            expect(near(x + w, 616) and near(y + h, 294) and w >= 236 and (h - 14) % 26 == 0, 'the choice box: right 616, bottom 294, width >= 236, rows 26 (%s)' % (ch[0][1:5],), t)
# the portrait textures of these frames, as uploaded (twiddled at load by src/portrait.cpp, de-twiddled by the host
# engine), must equal a shipped .dtx exactly: the twiddle round trip loses nothing
import glob
ROMDISK = 'romdisk'
shipped = {}
for f in glob.glob(os.path.join(ROMDISK, 'portraits', '*.dtx')):
    shipped[open(f, 'rb').read()[32:]] = os.path.basename(f)
ports = set()
for name, t in want.items():
    for ln in open(os.path.join(dump_dir, 'frame_%05d.txt' % t)):
        p = ln.split()
        if len(p) == 7 and p[0] == 'tex' and p[2] == 'mem' and p[3:6] == ['128', '128', '1'] and '_v' in p[6]:   # uploaded (not the blank slot)
            ports.add(p[6])
matched = [shipped.get(open(os.path.join(dump_dir, f), 'rb').read()) for f in sorted(ports)]
expect(len(ports) >= 2 and all(matched), 'portrait textures equal shipped .dtx files (%s)' % ', '.join(str(m) for m in matched), 0)

if len(want) < 3:
    errs.append('only %d of the frames were found: %s' % (len(want), sorted(want)))
if errs:
    print('layout: %d of %d checks failed' % (len(errs), checked))
    for e in errs[:12]:
        print('  ' + e)
    sys.exit(1)
print('layout: %d checks on %d frames (%s) within 1 px of the GDD 7.1 layout' % (checked, len(want), ', '.join(sorted(want))))
