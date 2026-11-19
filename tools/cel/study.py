#!/usr/bin/env python3
"""study.py - the cel-shading feasibility study's cost matrix (docs/CEL_SHADING_STUDY.md).

    python3 tools/cel/study.py [--out build/cel_study.json] [--jobs 4] [--quick]

Runs every budget scenario the suites use (tests/barrow T3, tests/battle3d's battle budget, the VS1 sweep) once per
configuration of the prototype (render3d.h CelParams, set with --tune) and reports, per scenario, the worst frame's
written vertices (the arena, the 18,000 limit), the worst SH4 estimate (1.520 x (40 xf + 24 out) + 134,813, the
1.60 M limit), dropped batches, and how many frames break each limit. Needs `make host`.
"""
import argparse, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
B32 = os.path.join(ROOT, 'build', 'battle_trace_f32')
LIM_V, LIM_E = 18000, 1.60

CONFIGS = {
    'base':        [],
    'toon':        ['cel_toon=1'],
    'chars':       ['cel_toon=1', 'cel_outline_px=1.5', 'cel_chars=1', 'cel_props=0', 'cel_lod=0'],
    'chars+small': ['cel_toon=1', 'cel_outline_px=1.5', 'cel_chars=1', 'cel_props=1', 'cel_lod=0'],
    'all':         ['cel_toon=1', 'cel_outline_px=1.5', 'cel_chars=1', 'cel_props=2', 'cel_lod=0'],
    'small only':  ['cel_toon=1', 'cel_outline_px=1.5', 'cel_chars=0', 'cel_props=1', 'cel_lod=0'],
    'hero':        ['cel_toon=1', 'cel_outline_px=1.5', 'cel_chars=2', 'cel_props=0', 'cel_lod=0'],
    'chars lod':   ['cel_toon=1', 'cel_outline_px=1.5', 'cel_chars=1', 'cel_props=0', 'cel_lod=1'],
    'hero lod':    ['cel_toon=1', 'cel_outline_px=1.5', 'cel_chars=2', 'cel_props=0', 'cel_lod=1'],
}

BATTLES = [('test_slimes', 'cycle:AFG'), ('test_slimes', 'fire'), ('slimes_2', 'random:3'), ('guard_duel', 'cycle:FGGA'),
           ('warden', 'boss:good:2'), ('warden', 'boss:naive:5')]


def est(xf, out):
    return (1.520 * (40.0 * xf + 24.0 * out) + 134813) / 1e6


def kv(line, skip):
    return dict(p.split('=', 1) for p in line.split()[skip:] if '=' in p)


ROMDISK = None


def run(args, tunes, stdout_file=None):
    cmd = [B32] + args + (['--romdisk', ROMDISK] if ROMDISK else [])
    for t in tunes:
        cmd += ['--tune', t]
    r = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=3000)
    return r.stdout


def summarise(rows):
    """rows: (arena, xf, out, drops) per frame"""
    if not rows:
        return None
    w = max(r[0] for r in rows)
    e = max(est(r[1], r[2]) for r in rows)
    return {'frames': len(rows), 'worst_arena': w, 'worst_est': round(e, 3), 'drops': sum(r[3] for r in rows),
            'over_v': sum(1 for r in rows if r[0] > LIM_V), 'over_e': sum(1 for r in rows if est(r[1], r[2]) > LIM_E)}


def sweep(tunes, extra):
    txt = run(['-', '/dev/null'] + extra + ['--sweep'], tunes)
    rows = []
    for ln in txt.splitlines():
        if ln.startswith('S '):
            d = kv(ln, 3)
            rows.append((int(d['arena']), int(d['xf']), int(d['out']), int(d['drops'])))
    return {'sweep': summarise(rows)}


def path(tunes, p):
    txt = run(['-', '/dev/null', '--path', p], tunes)
    rows = []
    for ln in txt.splitlines():
        if ln.startswith('V '):
            d = kv(ln, 2)
            rows.append((int(d['arena']), int(d['xf']), int(d['out']), int(d['drops'])))
    return {'path': summarise(rows)}


def playthrough(tunes, tmp):
    tr = os.path.join(tmp, 'full_%d.trace' % abs(hash(tuple(tunes))))
    run(['-', tr, '--state-only', '--view', '--boot', '--route', 'tests/barrow/routes/full.route', '--max-ticks', '90000'], tunes)
    by = {}
    for ln in open(tr):
        if ln.startswith('V ') or ln.startswith('B '):
            d = kv(ln, 1)
            mode = d.get('mode', 'battle') if ln.startswith('V ') else 'battle'
            by.setdefault(mode, []).append((int(d['arena']), int(d['xf']), int(d['out']), int(d['drops'])))
    os.remove(tr)
    return {m: summarise(r) for m, r in sorted(by.items())}


def battles(tunes, tmp):
    out = {}
    for enc, bot in BATTLES:
        tr = os.path.join(tmp, 'b_%s_%s_%d.trace' % (enc, bot.replace(':', '_'), abs(hash(tuple(tunes)))))
        run(['-', tr, '--battle-test', enc, '--state-only', '--bot', bot, '--bot-wait', '120', '--bot-linger', '120', '--view'], tunes)
        rows = []
        for ln in open(tr):
            if ln.startswith('B '):
                d = kv(ln, 1)
                rows.append((int(d['arena']), int(d['xf']), int(d['out']), int(d['drops'])))
        os.remove(tr)
        out['%s %s' % (enc, bot)] = summarise(rows)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'cel_study.json'))
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--configs', default=','.join(CONFIGS))
    ap.add_argument('--no-playthrough', action='store_true')
    ap.add_argument('--romdisk', default='', help='an alternative romdisk (e.g. other hull meshes)')
    a = ap.parse_args()
    global ROMDISK
    ROMDISK = a.romdisk or None
    tmp = os.path.join(ROOT, 'build', 'cel_tmp')
    os.makedirs(tmp, exist_ok=True)
    names = a.configs.split(',')
    jobs = []
    for n in names:
        t = CONFIGS[n]
        jobs.append((n, 'village sweep', lambda t=t: sweep(t, [])))
        jobs.append((n, 'barrow sweep', lambda t=t: sweep(t, ['--map', '/rd/maps/barrow.txt'])))
        jobs.append((n, 'title path', lambda t=t: path(t, 'title')))
        jobs.append((n, 'credits path', lambda t=t: path(t, 'credits')))
        jobs.append((n, 'battles', lambda t=t: battles(t, tmp)))
        if not a.no_playthrough:
            jobs.append((n, 'playthrough', lambda t=t: playthrough(t, tmp)))
    res = {n: {} for n in names}
    with ThreadPoolExecutor(a.jobs) as ex:
        futs = [(n, s, ex.submit(f)) for n, s, f in jobs]
        for n, s, f in futs:
            r = f.result()
            for k, v in r.items():
                res[n][s if k in ('sweep', 'path') else '%s: %s' % (s, k)] = v
            print('done %-12s %s' % (n, s), file=sys.stderr)
    json.dump(res, open(a.out, 'w'), indent=1)
    # table
    scen = sorted({s for n in names for s in res[n]})
    print('%-36s' % 'scenario' + ''.join('%22s' % n for n in names))
    for s in scen:
        cells = []
        for n in names:
            v = res[n].get(s)
            if not v:
                cells.append('%22s' % '-')
                continue
            flag = '!' if (v['over_v'] or v['over_e'] or v['drops']) else ' '
            cells.append('%22s' % ('%d %.2fM %s' % (v['worst_arena'], v['worst_est'], flag)))
        print('%-36s' % s[:36] + ''.join(cells))
    print('\n! = frames over 18,000 written vertices or 1.60 M estimated instructions, or dropped batches')


if __name__ == '__main__':
    main()
