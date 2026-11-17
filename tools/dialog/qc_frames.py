#!/usr/bin/env python3
"""qc_frames.py - render QC of the dialog slice: plays a route (paced, so pages type out), finds the moments worth a
look in its trace (the talk prompt, T3 mid-ease, a page typing, a choice, Rowan's page, a sys page, a fade, T4 ...),
dumps exactly those frames from a second identical run and renders them with tools/render_frame.py into one
labelled sheet.

    python3 tools/dialog/qc_frames.py ROUTE OUT.jpg [--pace 40] [--romdisk romdisk] [--cols 3] [--want name,...]
"""
import argparse, os, re, subprocess, sys, tempfile
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')


def moments(trace, want):
    G, N, Q, V = {}, [], [], {}
    for ln in open(trace):
        p = ln.split()
        if not p:
            continue
        if p[0] == 'G':
            G[int(p[1])] = dict(kv.split('=', 1) for kv in p[2:])
        elif p[0] == 'N':
            N.append((int(p[1]), p[2]))
        elif p[0] == 'Q':
            Q.append((int(p[1]), ln))
    found = {}
    def first(name, pred):
        if name in found:
            return
        for t in sorted(G):
            if pred(t, G[t]):
                found[name] = t
                return
    first('T3 mid-ease (bars sliding, camera easing)', lambda t, g: g['open'] == '1' and g['t'] == '8')
    first('a page typing (Wren)', lambda t, g: g['op'] == 'page' and g['spk'] == '1' and 10 < int(g['shown'].split('/')[0]) < int(g['shown'].split('/')[1]) - 8)
    first('a complete page, advance marker', lambda t, g: g['marker'] == '1' and g['op'] == 'page')
    first('a choice (3 options, the first selected)', lambda t, g: g['op'] == 'choice' and g['box'] == '1' and G.get(t - 12, {}).get('op') == 'choice')
    first("Rowan's page (his portrait slot)", lambda t, g: g['op'] == 'page' and g['spk'] == '0' and g['complete'] == '1')
    first('a sys page (gold, centred)', lambda t, g: g['op'] == 'sys' and g['complete'] == '1')
    first("Brom's page", lambda t, g: g['op'] == 'page' and g['spk'] == '2' and g['complete'] == '1')
    first('a fade (Brom\'s rest) with its sys page', lambda t, g: g['op'] == 'sys' and int(g['fade']) == 255 and g['complete'] == '1')
    first('T4 (bars out, the field camera back, input on)', lambda t, g: g['closing'] == '1' and g['t'] == '6')
    # field moments: the tick before a dialog opens shows the prompt
    opens = [t for t, m in N if m == 'dialog']
    if opens:
        found['the field: marker, objective, the "A Talk" prompt'] = opens[0] - 1
    later = [t for t in opens[1:]]
    if len(opens) > 2:
        found['a later talk prompt'] = opens[2] - 1
    battles = [t for t, m in N if m == 'battle']
    if battles:
        found['a battle (the first patrol, its intro)'] = battles[0] + 150
    if want:
        found = {k: v for k, v in found.items() if any(w in k for w in want)}
    return dict(sorted(found.items(), key=lambda kv: kv[1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('route'); ap.add_argument('out')
    ap.add_argument('--pace', type=int, default=40)
    ap.add_argument('--romdisk', default=os.path.join(ROOT, 'romdisk'))
    ap.add_argument('--cols', type=int, default=3)
    ap.add_argument('--want', default='')
    ap.add_argument('--scale', type=float, default=0.5)
    ap.add_argument('--extra', default='', help='extra battle_trace options')
    a = ap.parse_args()
    binp = os.path.join(ROOT, 'build', 'battle_trace_f32')
    tmp = tempfile.mkdtemp(prefix='qcf_')
    route = os.path.join(tmp, 'paced.route')
    with open(route, 'w') as f:
        f.write('pace %d\n' % a.pace + open(a.route).read())
    common = [binp, '-', os.path.join(tmp, 't.trace'), '--state-only', '--story', '--route', route, '--romdisk', a.romdisk,
              '--max-ticks', '90000'] + (a.extra.split() if a.extra else [])
    subprocess.run(common, capture_output=True)
    m = moments(os.path.join(tmp, 't.trace'), [w for w in a.want.split(',') if w])
    if not m:
        sys.exit('no moments found')
    dump = os.path.join(tmp, 'dump')
    os.makedirs(dump)
    subprocess.run(common + ['--dump', dump, '--dump-frames', ','.join(str(t) for t in m.values())], capture_output=True)
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import render_frame
    ims = []
    for label, t in m.items():
        p = os.path.join(dump, 'frame_%05d.txt' % t)
        if not os.path.exists(p):
            continue
        im = render_frame.render(p, a.romdisk).convert('RGB')
        w, h = int(640 * a.scale), int(480 * a.scale)
        im = im.resize((w, h), Image.LANCZOS)
        d = ImageDraw.Draw(im)
        d.rectangle([0, h - 16, w, h], fill=(0, 0, 0))
        d.text((4, h - 14), '%s (tick %d)' % (label, t), fill=(255, 230, 150))
        ims.append(im)
    cols = min(a.cols, len(ims))
    rows = (len(ims) + cols - 1) // cols
    w, h = ims[0].size
    sheet = Image.new('RGB', (cols * w + (cols - 1) * 4, rows * h + (rows - 1) * 4), (20, 20, 20))
    for k, im in enumerate(ims):
        sheet.paste(im, ((k % cols) * (w + 4), (k // cols) * (h + 4)))
    sheet.save(a.out, quality=88)
    print('wrote %s: %d frames' % (a.out, len(ims)))


if __name__ == '__main__':
    main()
