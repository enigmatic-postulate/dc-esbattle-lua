"""check_rules.py - an independent Python model of the slice's rules, checked against turn logs of
the C++ build (battle_trace --turn-log, game rules). Written from the GDD, not from the C++:

  damage   physical = max(1, STR + bonus - DEF), halved rounding up while the target guards;
           Fire / Grave Fire = max(1, floor(max(1, INT - MDEF) * 0.65)); Soul Rend bonus SOUL_REND
  guard    (GDD 11.1) present from the Guard until the guarding actor's next turn starts, never later
  burn     2 damage at the burning actor's turn start; Grave Fire leaves exactly one burn on its target
  potion   heals min(12, missing HP)
  boss     (GDD 12.4 / App. C) phase 1 rotation BS RD BS CH SR, phase 2 GF BS BW CH SR RD; Raise Dead
           skipped with 2 minions standing or within 3 Warden turns of the last one; Soul Rend only when
           charging; Bone Ward never twice in a row; the phase change on his first turn at <= 50 % HP,
           once, then phase 2 from its first entry; minions collapse when he falls
    python3 tests/battle3d/check_rules.py LOG [LOG ...]
Prints one line per log; exit 1 on any violation."""
import re
import sys

STATS = {   # str, def, mdef, int   (src/encounters.cpp; hero: party_default)
    'hero': (6, 2, 2, 6), 'slime_a': (4, 1, 1, 0), 'slime_b': (4, 1, 1, 0), 'slime_c': (4, 1, 1, 0), 'guard': (5, 2, 2, 0),
    'warden': (6, 1, 3, 6), 'minion_a': (3, 1, 1, 0), 'minion_b': (3, 1, 1, 0),
}
SOUL_REND = 10
P1 = ['bone_slash', 'raise_dead', 'bone_slash', 'charge', 'soul_rend']
P2 = ['grave_fire', 'bone_slash', 'bone_ward', 'charge', 'soul_rend', 'raise_dead']


def parse_state(line):
    m = re.match(r'ST active=(\S+) turn_i=(\d+) \|(.*)', line)
    actors = {}
    for tok in m.group(3).split():
        aid, rest = tok.split(':', 1)
        hp, rest2 = rest.split('/', 1)
        mx = re.match(r'(\d+)', rest2).group(1)
        st = re.findall(r'\+(\w+?)(\d+)\.(\d+)', rest2)
        actors[aid] = dict(hp=int(hp), max=int(mx), status=[(s, int(t), int(d)) for s, t, d in st])
    return m.group(1), actors


def has(a, s):
    return any(x[0] == s for x in a['status'])


def check(path):
    L = [l.rstrip('\n') for l in open(path) if l.strip()]
    errs = []
    states = []          # (index, active, actors)
    for i, l in enumerate(L):
        if l.startswith('ST'):
            states.append((i,) + parse_state(l))

    def state_before(i):
        s = [x for x in states if x[0] < i]
        return s[-1] if s else None

    def state_after(i):
        s = [x for x in states if x[0] > i]
        return s[0] if s else None

    # --- actions: damage / heal / statuses
    boss = dict(phase=1, rot=0, cd=0, last=None, done=False)
    for i, l in enumerate(L):
        if not l.startswith('ACT'):
            continue
        _, _, user, skill, _, *targets = l.split()
        b, a = state_before(i), state_after(i)
        if not b or not a:
            continue
        pre, post = b[2], a[2]
        tag = '%s line %d (%s %s)' % (path.split('/')[-1], i + 1, user, skill)

        def dmg_of(t):
            return pre[t]['hp'] - post[t]['hp']

        if skill in ('attack', 'bone_slash', 'soul_rend'):
            t = targets[0]
            s_u, s_t = STATS[user], STATS[t]
            base = max(1, s_u[0] + (SOUL_REND if skill == 'soul_rend' else 0) - s_t[1])
            if has(pre[t], 'guard'):
                base = max(1, (base + 1) // 2)
            want = min(base, pre[t]['hp'])
            if dmg_of(t) != want:
                errs.append('%s: %s took %d, expected %d' % (tag, t, dmg_of(t), want))
            if t == 'warden' and post[t]['hp'] <= 0:
                for k in ('minion_a', 'minion_b'):
                    if post[k]['hp'] != 0:
                        errs.append('%s: %s did not collapse with the Warden' % (tag, k))
        elif skill in ('fireball', 'grave_fire'):
            for t in targets:
                if pre[t]['hp'] <= 0:
                    continue
                base = max(1, STATS[user][3] - STATS[t][2])
                want = min(max(1, (base * 65) // 100), pre[t]['hp'])
                if t.startswith('minion') and 'warden' in post and post['warden']['hp'] <= 0:
                    want = pre[t]['hp']                          # the same blow felled the Warden: they collapse
                if dmg_of(t) != want:
                    errs.append('%s: %s took %d, expected %d' % (tag, t, dmg_of(t), want))
                if skill == 'grave_fire' and post[t]['hp'] > 0:
                    n = sum(1 for x in post[t]['status'] if x[0] == 'burn')
                    if n != 1:
                        errs.append('%s: %s has %d burns after Grave Fire (expected exactly 1)' % (tag, t, n))
        elif skill == 'potion':
            want = min(12, pre[user]['max'] - pre[user]['hp'])
            if -dmg_of(user) != want:
                errs.append('%s: healed %d, expected %d' % (tag, -dmg_of(user), want))
        elif skill in ('guard', 'bone_ward'):
            if not has(post[user], 'guard'):
                errs.append('%s: no guard after guarding' % tag)

        # --- the Bone Warden's rotation
        if user == 'warden':
            w = pre['warden']
            minions = sum(1 for k in ('minion_a', 'minion_b') if pre.get(k, {'hp': 0})['hp'] > 0)
            if boss['cd'] > 0:
                boss['cd'] -= 1
            if not boss['done'] and w['hp'] * 2 <= w['max']:
                want = 'phase'
                boss.update(done=True, phase=2, rot=0, cd=0, last='phase')
            else:
                rot = P1 if boss['phase'] == 1 else P2
                want = None
                for _ in range(len(rot)):
                    sk = rot[boss['rot']]
                    boss['rot'] = (boss['rot'] + 1) % len(rot)
                    if sk == 'raise_dead' and (minions >= 2 or boss['cd'] > 0):
                        continue
                    if sk == 'soul_rend' and not has(w, 'charge'):
                        continue
                    if sk == 'bone_ward' and boss['last'] == 'bone_ward':
                        continue
                    want = sk
                    break
                if want == 'raise_dead':
                    boss['cd'] = 3
                boss['last'] = want
            if skill != want:
                errs.append('%s: the Warden used %s, the rotation says %s' % (tag, skill, want))
            if skill == 'raise_dead':
                for k in ('minion_a', 'minion_b'):
                    if post[k]['hp'] != post[k]['max']:
                        errs.append('%s: %s did not rise (hp %d)' % (tag, k, post[k]['hp']))

    # --- guard lifetime: until the guarding actor's next turn starts
    for i, l in enumerate(L):
        if not l.startswith('ACT') or l.split()[3] not in ('guard', 'bone_ward'):
            continue
        who = l.split()[2]
        later = [s for s in states if s[0] > i]
        # through the other actors' turns: still guarding; at the guarding actor's next turn: gone
        for k, s in enumerate(later):
            act, acts = s[1], s[2]
            if act == who and k > 0 and later[k - 1][1] != who:
                if has(acts[who], 'guard'):
                    errs.append('%s line %d: %s still guards at its next turn' % (path.split('/')[-1], s[0] + 1, who))
                break
            if act != who and acts[who]['hp'] > 0 and not has(acts[who], 'guard'):
                errs.append('%s line %d: %s lost its guard before its next turn' % (path.split('/')[-1], s[0] + 1, who))
                break

    # --- burn ticks (state changes without an action): 2 per burning actor at its turn start
    for k in range(1, len(states)):
        i, act, cur = states[k]
        if L[i - 1].startswith('ACT'):
            continue
        prev = states[k - 1][2]
        for aid, a in cur.items():
            d = prev[aid]['hp'] - a['hp']
            if d > 0:
                burns = sum(1 for x in prev[aid]['status'] if x[0] == 'burn')
                if d > 2 * max(1, burns) or burns == 0:
                    if not (aid.startswith('minion') and prev.get('warden', {'hp': 1})['hp'] <= 0):
                        errs.append('%s line %d: %s lost %d HP outside an action with %d burn(s)' % (path.split('/')[-1], i + 1, aid, d, burns))

    # --- minions fall with the Warden
    for i, act, acts in states:
        if 'warden' in acts and acts['warden']['hp'] <= 0:
            for k in ('minion_a', 'minion_b'):
                if acts[k]['hp'] > 0:
                    errs.append('%s line %d: %s stands after the Warden fell' % (path.split('/')[-1], i + 1, k))
    return errs


if __name__ == '__main__':
    bad = 0
    for p in sys.argv[1:]:
        e = check(p)
        bad += len(e)
        for x in e[:6]:
            print('  ' + x)
    print('rules model: %d logs checked, %d violations' % (len(sys.argv) - 1, bad))
    sys.exit(1 if bad else 0)
