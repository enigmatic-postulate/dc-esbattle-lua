#!/usr/bin/env python3
"""check_dialog.py - the dialog UI rules (docs/NEXT_SLICE_DIALOG.md 5.2-5.3, GDD 4 / 6 T3 T4 / 7.1 / 7.3 / 7.5)
checked tick by tick on a host trace with --story lines (G: dialog UI, I: input, N: mode changes), against an
independent model. Needs the compiled scripts (lint.py --dump of the same script) for the pages and choices.

    python3 tests/dialog/check_dialog.py TRACE DUMP [--expect-sessions N] [--require rule,rule]

Rules (each reported with the number of ticks it was checked on):
  T3-bars   bars = 36 x ease_out((t+1)/12) while open, 36 from tick 11
  T3-box    no box before tick 12; the box (with the first page) at tick 12
  T3-cam    camera weight = smoothstep(t/20) x 1000 (for an NPC dialog), 1000 from tick 20
  T4        on the closing tick the mode is the field; bars out over 12 ticks from their height, camera out over 20
  type      the typewriter: +1 glyph a tick (+4 with B held), a pause of 6 after . ! ? and 3 after , (2 / 1 with B),
            one pause after a run (...), none at the end; the first glyph on the page's first visible tick
  A-complete  A on a page still typing shows it whole that tick
  A-next    A on a complete page leaves it that tick (the next page, a choice, a wait/fade, or the end)
  marker    the advance marker shows exactly when the page is complete and what follows is not a choice
  choice    a choice opens with the first option selected; up/down edges and the stick crossing +-64 move it,
            without wrapping; A picks; a fresh B picks the cancel option if there is one, else nothing;
            nothing is taken on the tick the choice appears
  consume   a dialog never opens on the tick another closes; every dialog opens on a fresh A press
  hud       the field HUD and the talk prompt are hidden while a dialog is open
  portrait  the speaking NPC's portrait is in slot 0, Rowan's in slot 1; sys pages show none; a slot is only
            rewritten after 2 frames off screen (the hardware-safe upload)
  fade      a fade blocks with the box hidden; its level moves linearly over the fade's ticks
"""
import re
import sys
from collections import defaultdict

STOP = set('.!?')


def ease_out(u):
    u = max(0.0, min(1.0, u))
    return 1.0 - (1.0 - u) * (1.0 - u)


def smooth(u):
    u = max(0.0, min(1.0, u))
    return u * u * (3.0 - 2.0 * u)


def load_dump(path):
    ops, speakers = {}, {}
    for ln in open(path):
        ln = ln.rstrip('\n')
        m = re.match(r'speaker (\d+) (\S+) ', ln)
        if m:
            speakers[int(m.group(1))] = m.group(2)
            continue
        m = re.match(r'(\d+) (\w+)(.*)$', ln)
        if not m:
            continue
        pc, code, rest = int(m.group(1)), m.group(2), m.group(3).strip()
        op = dict(code=code)
        if code in ('PAGE', 'SYS'):
            q = rest.index('"')
            head = rest[:q].split()
            op['text'] = bytes(rest[q + 1:-1], 'latin-1').decode('unicode_escape')
            op['glyphs'] = int(head[-1])
            if code == 'PAGE':
                op['speaker'], op['variant'] = head[0], head[1]
        elif code == 'CHOICE':
            n, c = rest.split()
            op['n'] = int(n)
            op['cancel'] = None if c == 'cancel=-' else int(c.split('=')[1])
        elif code == 'OPTION':
            op['target'] = int(rest.split()[1])
        elif code in ('JMP',):
            op['target'] = int(rest)
        elif code == 'JZ':
            op['target'] = int(rest.split()[0])
        elif code in ('WAIT', 'FADE_OUT', 'FADE_IN'):
            op['ticks'] = int(rest)
        ops[pc] = op
    return ops, speakers


def visible(text):
    out, i = [], 0
    while i < len(text):
        if text[i] == '{':
            j = text.find('}', i)
            if j >= 0:
                i = j + 1
                continue
        if text[i] != '\n':
            out.append(text[i])
        i += 1
    return out


def follow(ops, pc):
    for _ in range(16):
        if ops.get(pc, {}).get('code') != 'JMP':
            return pc
        pc = ops[pc]['target']
    return pc


def reachable_blocking(ops, start):
    """Blocking ops (or END) reachable from `start` without passing another blocking op."""
    out, seen, work = set(), set(), [start]
    while work:
        pc = work.pop()
        if pc in seen or pc not in ops:
            continue
        seen.add(pc)
        c = ops[pc]['code']
        if c in ('PAGE', 'SYS', 'CHOICE', 'WAIT', 'FADE_OUT', 'FADE_IN', 'END'):
            out.add(pc)
            continue
        if c == 'JMP':
            work.append(ops[pc]['target'])
        elif c == 'JZ':
            work += [pc + 1, ops[pc]['target']]
        else:
            work.append(pc + 1)
    return out


def parse(trace):
    G, I, N = {}, {}, []
    for ln in open(trace):
        if ln.startswith('G '):
            p = ln.split()
            d = dict(tick=int(p[1]))
            for kv in p[2:]:
                k, v = kv.split('=', 1)
                d[k] = v
            for k in ('open', 'closing', 't', 'bars', 'cam', 'box', 'pc', 'page', 'pause', 'complete', 'marker', 'choice', 'sel',
                      'opts', 'cancel', 'spk', 'var', 'slot', 'slide', 'port', 'fade', 'prompt', 'hud', 'sy', 'uploads'):
                d[k] = int(d[k])
            d['btn'] = int(d['btn'], 16)
            s, tot = d['shown'].split('/')
            d['shown'], d['total'] = int(s), int(tot)
            d['slots'] = [tuple(int(x) for x in sv.split('.')) for sv in d['slots'].split(',')]
            G[d['tick']] = d
        elif ln.startswith('I '):
            p = ln.split()
            I[int(p[1])] = (int(p[2], 16), int(p[3]), int(p[4]))
        elif ln.startswith('N '):
            p = ln.split()
            N.append((int(p[1]), p[2]))
    return G, I, N


A, B, UP, DOWN = 0x10, 0x20, 0x01, 0x02


def main():
    args = sys.argv[1:]
    expect_sessions = None
    if '--expect-sessions' in args:
        i = args.index('--expect-sessions'); expect_sessions = int(args[i + 1]); del args[i:i + 2]
    require = []
    if '--require' in args:
        i = args.index('--require'); require = args[i + 1].split(','); del args[i:i + 2]
    trace, dump = args[0], args[1]
    ops, speakers = load_dump(dump)
    rowan = [k for k, v in speakers.items() if v == 'rowan']
    rowan = rowan[0] if rowan else -1
    G, I, N = parse(trace)
    errs = []
    count = defaultdict(int)

    def err(rule, tick, msg):
        if len(errs) < 40:
            errs.append('%s tick %d: %s' % (rule, tick, msg))
        count[rule + '!'] += 1

    def btn(t):
        return I.get(t, (0, 0, 0))[0]

    def edge(t, b):
        return (btn(t) & b) and not (btn(t - 1) & b)

    ticks = sorted(G)
    # ---- mode changes: sessions, consume
    opens = [t for t, m in N if m == 'dialog']
    closes = [t for t, m in N if m != 'dialog' and any(t2 < t for t2 in opens)]
    for t in opens:
        count['consume'] += 1
        if any(tc == t for tc, m in N if m == 'field'):
            err('consume', t, 'a dialog opened on the tick one closed')
        if t not in G or G[t]['t'] != 0:
            err('consume', t, 'the dialog did not start at T3 tick 0')
        # a dialog started by a talk (not after a battle) needs a fresh A press that tick
        prev_modes = [m for tt, m in N if tt < t]
        if prev_modes and prev_modes[-1] == 'field' and not edge(t, A):
            # the after_battle script opens from to_field -> field -> dialog in one tick: not a press
            if not any(tt == t for tt, m in N if m == 'field'):
                err('consume', t, 'a dialog opened without a fresh A press')
    if expect_sessions is not None and len(opens) != expect_sessions:
        errs.append('sessions: %d dialogs opened, expected %d' % (len(opens), expect_sessions))

    # ---- per tick
    prev = None
    sim = None            # typewriter model: dict(pc, shown, pause)
    fade_ref = None
    for t in ticks:
        g = G[t]
        p = prev if prev is not None and prev['tick'] == t - 1 else None
        if g['open']:
            count['hud'] += 1
            if g['hud'] or g['prompt'] != -1:
                err('hud', t, 'the HUD or the talk prompt is shown during a dialog (hud=%d prompt=%d)' % (g['hud'], g['prompt']))
            # T3
            if g['t'] < 12 or (p and p['open'] and p['bars'] < 36):
                want = int(36 * ease_out(min(g['t'] + 1, 12) / 12.0) + 0.5)
                count['T3-bars'] += 1
                if g['bars'] != want:
                    err('T3-bars', t, 'bars %d, want %d at T3 tick %d' % (g['bars'], want, g['t']))
            if g['t'] < 12:
                count['T3-box'] += 1
                if g['box']:
                    err('T3-box', t, 'the box shows before T3 tick 12')
            if g['t'] == 12 and g['op'] in ('page', 'sys', 'choice'):
                count['T3-box'] += 1
                if not g['box']:
                    err('T3-box', t, 'no box at T3 tick 12')
            if g['t'] <= 20 and g['cam'] > 0 or (g['t'] >= 20 and g['cam'] == 1000):
                want = int(smooth(min(g['t'], 20) / 20.0) * 1000 + 0.5)
                count['T3-cam'] += 1
                if abs(g['cam'] - want) > 1:
                    err('T3-cam', t, 'camera weight %d, want %d at T3 tick %d' % (g['cam'], want, g['t']))
            # typewriter model
            if g['box'] and g['op'] in ('page', 'sys') and g['page'] >= 0:
                op = ops[g['page']]
                vis = visible(op['text'])
                fast = bool(btn(t) & B)
                if sim is None or sim['pc'] != g['page'] or not (p and p['box']):
                    sim = dict(pc=g['page'], shown=0, pause=0)
                    first = True
                else:
                    first = False
                if not first and edge(t, A) and sim['shown'] < len(vis):
                    sim['shown'], sim['pause'] = len(vis), 0
                    count['A-complete'] += 1
                    if g['shown'] != len(vis):
                        err('A-complete', t, 'A on a typing page did not complete it (%d/%d)' % (g['shown'], len(vis)))
                elif sim['shown'] < len(vis):
                    if sim['pause'] > 0:
                        sim['pause'] -= 1
                    else:
                        for _ in range(4 if fast else 1):
                            if sim['shown'] >= len(vis):
                                break
                            c = vis[sim['shown']]
                            sim['shown'] += 1
                            if sim['shown'] >= len(vis):
                                break
                            nx = vis[sim['shown']]
                            if c in STOP and nx not in STOP:
                                sim['pause'] = 2 if fast else 6
                                break
                            if c == ',' and nx != ',':
                                sim['pause'] = 1 if fast else 3
                                break
                count['type'] += 1
                if (g['shown'], g['pause']) != (sim['shown'], sim['pause']):
                    err('type', t, 'page %d shown %d pause %d, model %d / %d (B %s)' % (g['page'], g['shown'], g['pause'], sim['shown'],
                                                                                        sim['pause'], 'held' if fast else 'up'))
                    sim['shown'], sim['pause'] = g['shown'], g['pause']
                # advance marker
                nxt = ops.get(follow(ops, g['page'] + 1), {}).get('code')
                want = g['complete'] and g['op'] != 'choice' and nxt != 'CHOICE'
                count['marker'] += 1
                if bool(g['marker']) != bool(want):
                    err('marker', t, 'advance marker %d, want %d' % (g['marker'], int(want)))
                # portraits
                if op['code'] == 'PAGE':
                    count['portrait'] += 1
                    want_slot = 1 if g['spk'] == rowan else 0
                    if g['slot'] != want_slot:
                        err('portrait', t, 'speaker %d in slot %d, want %d' % (g['spk'], g['slot'], want_slot))
                else:
                    count['portrait'] += 1
                    if g['port'] or g['spk'] != -1:
                        err('portrait', t, 'a sys page shows a portrait / a speaker')
            # A on a complete page leaves it
            if p and p['open'] and p['box'] and p['page'] >= 0 and p['op'] in ('page', 'sys') and p['complete'] and edge(t, A):
                count['A-next'] += 1
                if g['page'] == p['page'] and g['op'] in ('page', 'sys') and g['pc'] == p['pc']:
                    err('A-next', t, 'A on a complete page did not leave it')
            # choices
            if g['choice'] >= 0 and g['box']:
                c = ops[g['choice']]
                if not p or p['choice'] != g['choice']:
                    count['choice'] += 1
                    if g['sel'] != 0:
                        err('choice', t, 'a choice opened with option %d selected' % g['sel'])
                else:
                    sel = p['sel']
                    sy, psy = I.get(t, (0, 0, 0))[2], I.get(t - 1, (0, 0, 0))[2]
                    if edge(t, UP) or (sy <= -64 and psy > -64):
                        sel = max(0, sel - 1)
                    if edge(t, DOWN) or (sy >= 64 and psy < 64):
                        sel = min(c['n'] - 1, sel + 1)
                    count['choice'] += 1
                    if g['sel'] != sel:
                        err('choice', t, 'selection %d, model %d' % (g['sel'], sel))
            if p and p['choice'] >= 0 and p['box'] and p['open']:
                c = ops[p['choice']]
                first_tick = not (G.get(t - 2) and G[t - 2]['choice'] == p['choice'])
                picked = None
                if edge(t, A):
                    picked = p['sel']
                elif edge(t, B) and c['cancel'] is not None:
                    picked = c['cancel']
                count['choice'] += 1
                still = g['choice'] == p['choice'] and g['open']
                if first_tick and (edge(t - 1, A) or edge(t - 1, B)):
                    pass
                if picked is None:
                    if not still and g['open']:
                        err('choice', t, 'the choice closed without A or a fresh B on a cancel option')
                    if edge(t, B) and c['cancel'] is None and not still:
                        err('choice', t, 'B closed a choice that has no cancel option')
                else:
                    if still:
                        err('choice', t, 'the choice ignored %s' % ('A' if edge(t, A) else 'B'))
                    else:
                        tgt = ops[p['choice'] + 1 + picked]['target']
                        reach = reachable_blocking(ops, tgt)
                        if g['open'] and g['pc'] not in reach:
                            err('choice', t, 'picked option %d but the script went to %d (reachable: %s)' % (picked, g['pc'], sorted(reach)))
            # fades and waits: no box
            if g['op'] in ('fade', 'wait'):
                count['fade'] += 1
                if g['box']:
                    err('fade', t, 'the box shows during a %s' % g['op'])
                if g['op'] == 'fade':
                    o = ops[g['pc']]
                    if fade_ref is None or fade_ref[0] != g['pc']:
                        fade_ref = (g['pc'], p['fade'] if p else 0, 255 if o['code'] == 'FADE_OUT' else 0, o['ticks'], t)
                    pc0, f0, f1, n, t0 = fade_ref
                    k = min(n, t - t0 + 1)
                    want = f0 + int((f1 - f0) * k / n)          # C integer division: toward zero
                    if g['fade'] != want:
                        err('fade', t, 'fade level %d, want %d' % (g['fade'], want))
        # portrait uploads only after 2 frames off screen
        if p:
            for k in (0, 1):
                if g['slots'][k] != p['slots'][k] and p['slots'][k][0] >= 0:
                    count['portrait'] += 1
                    shown_recent = any(G.get(t - d, {}).get('port') and G[t - d].get('slot') == k for d in (1, 2))
                    if shown_recent:
                        err('portrait', t, 'slot %d rewritten while it was on screen in the last 2 frames' % k)
        # T4
        if g['closing'] and not g['open']:
            count['T4'] += 1
            start = max(tt for tt in ticks if tt <= t and G[tt]['closing'] and G[tt]['t'] == 0) if any(
                G[tt]['closing'] and G[tt]['t'] == 0 for tt in ticks if tt <= t) else t
            b0 = G.get(start - 1, {}).get('bars', 36)
            want = int(b0 * (1.0 - ease_out(min(g['t'] + 1, 12) / 12.0)) + 0.5)
            if g['bars'] != want:
                err('T4', t, 'closing bars %d, want %d' % (g['bars'], want))
            if g['t'] == 0 and not any(tt == t and m == 'field' for tt, m in N):
                err('T4', t, 'the mode is not the field on the closing tick')
            if g['t'] >= 20 and g['cam'] != 0:
                err('T4', t, 'the camera is not back after 20 ticks')
        prev = g
    for r in require:
        if count[r] == 0:
            errs.append('rule %s was never exercised' % r)
    summary = ', '.join('%s %d' % (k, v) for k, v in sorted(count.items()) if not k.endswith('!'))
    if errs:
        print('dialog UI: %d violations (%s)' % (len(errs), summary))
        for e in errs[:25]:
            print('  ' + e)
        return 1
    print('dialog UI: %d dialogs, rules checked: %s - OK' % (len(opens), summary))
    return 0


if __name__ == '__main__':
    sys.exit(main())
