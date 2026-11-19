#!/usr/bin/env python3
"""check_results.py - the results card (GDD 14 E6; docs/NEXT_SLICE_BARROW.md 11 T10) against counters recomputed from the
run: a --boot --story trace (its R line = what the card shows; N, T and Q lines) and the --turn-log of the same run.

    python3 tests/barrow/check_results.py TRACE TURNLOG

  battles won   = battles entered (N to_battle) - battles lost (N death)              (since the new game)
  turns taken   = the hero's resolved actions (ACT lines of the turn log)
  potions used  = the hero's potion actions
  damage taken  >= the hero's HP lost (the rules' numbers; a killing blow may overshoot the HP left)
  damage dealt  >= the enemies' HP lost to the hero and burn (the same; the boss's fall takes his minions without a blow)
  retries       = the Q line's retries counter when the card opens
  play time     = the ticks from the new game (the T2 card) to the card, in whole ticks
  rank          = GDD 14 / decision D8: Gold < 20 min and 0 retries; Silver < 30 min and <= 2 retries; else Bronze
"""
import re
import sys


def main():
    trace, tlog = sys.argv[1], sys.argv[2]
    R, N, Q = None, [], []
    for ln in open(trace):
        if ln.startswith('R '):
            p = ln.split()
            R = dict(kv.split('=') for kv in p[2:])
            R = {k: int(v) for k, v in R.items()}
            R['tick'] = int(p[1])
        elif ln.startswith('N '):
            p = ln.split()
            N.append((int(p[1]), p[2]))
        elif ln.startswith('Q '):
            Q.append((int(ln.split()[1]), int(re.search(r' retries=(\d+)', ln).group(1))))
    if R is None:
        print('results: no R line (the card never opened)'); return 1
    errs = []
    start = max(t for t, m in N if m == 'card' and t < R['tick'])
    in_session = [(t, m) for t, m in N if start <= t <= R['tick']]
    entered = sum(1 for t, m in in_session if m == 'to_battle')
    lost = sum(1 for t, m in in_session if m == 'death')
    if R['battles_won'] != entered - lost:
        errs.append('battles won %d, the trace has %d battles and %d losses' % (R['battles_won'], entered, lost))
    retries = max((r for t, r in Q if t <= R['tick']), default=0)
    if R['retries'] != retries:
        errs.append('retries %d, the trace says %d' % (R['retries'], retries))
    if abs(R['play_ticks'] - (R['tick'] - start)) > 1:
        errs.append('play time %d ticks, the new game began %d ticks before the card' % (R['play_ticks'], R['tick'] - start))
    turns = potions = 0
    hero_lost = enemy_lost = 0
    prev = None
    for ln in open(tlog):
        if ln.startswith('ACT '):
            p = ln.split()
            if p[2] == 'hero':
                turns += 1
                potions += p[3] == 'potion'
        elif ln.startswith('ST '):
            hp = dict((a, int(h)) for a, h, _ in re.findall(r' (\w+):(\d+)/(\d+)', ln))
            if prev is not None and set(hp) == set(prev):
                for a, h in hp.items():
                    d = prev[a] - h
                    if d > 0:
                        if a == 'hero':
                            hero_lost += d
                        else:
                            enemy_lost += d
            prev = hp
        elif ln.startswith('END'):
            prev = None
    if R['hero_turns'] != turns:
        errs.append('turns taken %d, the turn log has %d hero actions' % (R['hero_turns'], turns))
    if R['potions_used'] != potions:
        errs.append('potions used %d, the turn log has %d' % (R['potions_used'], potions))
    if R['dmg_taken'] < hero_lost:
        errs.append('damage taken %d < the hero\'s HP lost %d' % (R['dmg_taken'], hero_lost))
    if R['dmg_dealt'] <= 0 or R['dmg_dealt'] > enemy_lost + 200:
        errs.append('damage dealt %d, the enemies lost %d HP' % (R['dmg_dealt'], enemy_lost))
    minutes = R['play_ticks'] // 3600
    want = 0 if (minutes < 20 and R['retries'] == 0) else (1 if (minutes < 30 and R['retries'] <= 2) else 2)
    if R['rank'] != want:
        errs.append('rank %d, D8 gives %d' % (R['rank'], want))
    if errs:
        print('results: %d problems' % len(errs))
        for e in errs:
            print('  ' + e)
        return 1
    print('results card == the run: %d battles won, %d turns, %d potions, %d retries, %d:%02d played, damage %d dealt / %d taken '
          '(HP lost: enemies %d, hero %d), rank %s' % (R['battles_won'], R['hero_turns'], R['potions_used'], R['retries'],
                                                         R['play_ticks'] // 3600, R['play_ticks'] // 60 % 60, R['dmg_dealt'], R['dmg_taken'],
                                                         enemy_lost, hero_lost, ['gold', 'silver', 'bronze'][R['rank']]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
