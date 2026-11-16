"""boss_tune.py - grid search of the Bone Warden's stats against the GDD 12.6 balance targets, with
the harness bots (tests/battle3d/boss_balance.sh measures one setting). Host tool; parallel.
    python3 tools/battle3d/boss_tune.py [seeds] > results.txt
Targets: good play wins >= 95 % and ends with 8-20 HP; naive play wins 30-60 %; the no-guard bot
loses more than good play, mostly to Soul Rend; good play takes 12-16 hero turns. Tuning changes
stats only (the formulas stay), and prefers the smallest change from the GDD starting values."""
import itertools, os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
N = int(sys.argv[1]) if len(sys.argv) > 1 else 50
START = dict(m_str=4, m_hp=7, w_hp=44, w_str=6, w_def=1, rend=8)
GRID = dict(m_str=[3, 4], m_hp=[5, 7], w_hp=[36, 40, 44], w_str=[5, 6], w_def=[1, 2], rend=[8, 10, 12])
if os.environ.get('GRID') == 'fine':
    GRID = dict(m_str=[3], m_hp=[6, 7], w_hp=[40, 42, 44], w_str=[6], w_def=[1, 2], rend=[9, 10, 11])

def run(mode, seed, cfg, tmp):
    s = 'minion_a.str=%d,minion_b.str=%d,minion_a.hp=%d,minion_b.hp=%d,warden.hp=%d,warden.str=%d,warden.def=%d,rend.bonus=%d' % (
        cfg['m_str'], cfg['m_str'], cfg['m_hp'], cfg['m_hp'], cfg['w_hp'], cfg['w_str'], cfg['w_def'], cfg['rend'])
    log = os.path.join(tmp, '%s_%d.log' % (mode, seed))
    subprocess.run([os.path.join(ROOT, 'build', 'battle_trace_f64'), '-', '/dev/null', '--battle-test', 'warden', '--state-only',
                    '--view2d', '--bot', 'boss:%s:%d' % (mode, seed), '--set', s, '--turn-log', log], check=True, cwd=ROOT)
    L = open(log).read().splitlines()
    won = any(l.startswith('END outcome=won') for l in L)
    acts = [l for l in L if l.startswith('ACT')]
    hero_turns = sum(1 for a in acts if a.split()[2] == 'hero')
    hp = int(L[[i for i, l in enumerate(L) if l.startswith('ST')][-1]].split(' hero:')[1].split('/')[0])
    rend_loss = (not won) and acts and 'soul_rend' in acts[-1]
    return won, hp, hero_turns, rend_loss

def measure(cfg):
    with tempfile.TemporaryDirectory() as tmp:
        r = {}
        for mode in ('good', 'naive', 'noguard'):
            res = [run(mode, s, cfg, tmp) for s in range(1, N + 1)]
            wins = [x for x in res if x[0]]
            r[mode] = dict(win=100.0 * len(wins) / N, hp=(sum(x[1] for x in wins) / len(wins)) if wins else 0,
                           turns=sum(x[2] for x in res) / N, rend=sum(1 for x in res if x[3]), losses=N - len(wins))
        return r

def score(r):
    g, n, q = r['good'], r['naive'], r['noguard']
    s = max(0, 95 - g['win']) * 3
    s += max(0, 8 - g['hp']) * 2 + max(0, g['hp'] - 20) * 2
    s += max(0, 30 - n['win']) + max(0, n['win'] - 60)
    s += max(0, q['win'] - (g['win'] - 15))                       # no guard: clearly worse than good play
    s += 0 if q['losses'] == 0 else max(0, 50 - 100.0 * q['rend'] / q['losses']) * 0.5
    s += max(0, 12 - g['turns']) * 2 + max(0, g['turns'] - 16) * 2
    return s

def changes(cfg):
    return sum(1 for k in cfg if cfg[k] != START[k])

keys = list(GRID)
cfgs = [dict(zip(keys, v)) for v in itertools.product(*[GRID[k] for k in keys])]
with ThreadPoolExecutor(4) as ex:
    results = list(ex.map(measure, cfgs))
rows = sorted(zip(cfgs, results), key=lambda cr: (round(score(cr[1]), 1), changes(cr[0])))
for cfg, r in rows[:25]:
    print('score %6.1f changes %d  %s  | good %3.0f%% hp %4.1f turns %4.1f | naive %3.0f%% | noguard %3.0f%% (%d/%d losses to Soul Rend)' % (
        score(r), changes(cfg), ' '.join('%s=%d' % kv for kv in cfg.items()), r['good']['win'], r['good']['hp'], r['good']['turns'],
        r['naive']['win'], r['noguard']['win'], r['noguard']['rend'], r['noguard']['losses']))
