"""build_anims.py - builds the shared animation library romdisk/anims/rig_medium.dma from the
KayKit Rig_Medium animation packs (docs/DMA.md). Runs in Blender's Python:

    BPY=.venv/bin/python
    $BPY tools/battle3d/build_anims.py --kaykit ../KayKit --out romdisk/anims/rig_medium.dma \
         --report tools/battle3d/reports/anims.json
    $BPY tools/battle3d/build_anims.py --check     # convention check against the shipped models

Every KayKit character (Knight, the three skeletons, Mage, Barbarian, Rogue...) uses Rig_Medium
with identical rest transforms, so one copy of each clip serves them all.

Per clip the library stores: frames at 30 fps (looping clips drop the duplicated last frame, as the
field models' clips do), 16-bit quaternions for every bone, float translations for the bones the
clip moves away from the bind pose (hips always; e.g. the right leg that falls off in
Skeletons_Death), up to 4 event times in 60 Hz ticks (impact, release, ...) and a bounding sphere
of the joints over the whole clip (culling; clips such as the death slide move the body).

Events are detected from the motion and can be overridden in CLIPS (see EVENT_DOC).
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmfio  # noqa: E402

EVENT_DOC = """events[0] = impact (weapon tip: the first frame after the tip's speed peak where it has
slowed to 35 % of that peak - the blade meets its target and stops), events[1] = release (throw:
the right hand's speed peak), events[2] = collapse (death clips: the head comes within 5 cm of its
lowest point).
Values are 60 Hz ticks from the clip start."""

# name in the library, pack, action, loop, event rule, override events (ticks) or None
#   event rules: 'tip' (impact by weapon tip, right hand), 'tip_l' (left hand), 'throw', 'death', None
CLIPS = [
    # -- general
    ('idle_a',      'General',          'Idle_A',                      True,  None,    None),
    ('hit_a',       'General',          'Hit_A',                       False, None,    None),
    ('hit_b',       'General',          'Hit_B',                       False, None,    None),
    ('death_a',     'General',          'Death_A',                     False, 'death', None),
    ('death_b',     'General',          'Death_B',                     False, 'death', None),
    ('throw',       'General',          'Throw',                       False, 'throw', (dmfio.EV_NONE, 46)),  # detected 38: arm still rising
    ('use_item',    'General',          'Use_Item',                    False, None,    None),
    ('interact',    'General',          'Interact',                    False, None,    None),
    # -- melee
    ('chop',        'CombatMelee',      'Melee_1H_Attack_Chop',        False, 'tip',   None),
    ('slice_d',     'CombatMelee',      'Melee_1H_Attack_Slice_Diagonal', False, 'tip', None),
    ('slice_h',     'CombatMelee',      'Melee_1H_Attack_Slice_Horizontal', False, 'tip', None),
    ('jump_chop',   'CombatMelee',      'Melee_1H_Attack_Jump_Chop',   False, 'tip',   None),
    ('stab',        'CombatMelee',      'Melee_1H_Attack_Stab',        False, 'tip',   None),
    ('chop_2h',     'CombatMelee',      'Melee_2H_Attack_Chop',        False, 'tip',   None),
    ('idle_2h',     'CombatMelee',      'Melee_2H_Idle',               True,  None,    None),
    ('idle_melee',  'CombatMelee',      'Melee_Unarmed_Idle',          True,  None,    None),
    ('block',       'CombatMelee',      'Melee_Block',                 False, None,    None),
    ('blocking',    'CombatMelee',      'Melee_Blocking',              True,  None,    None),
    ('block_hit',   'CombatMelee',      'Melee_Block_Hit',             False, None,    None),
    # -- movement
    ('run',         'MovementBasic',    'Running_A',                   True,  None,    None),
    ('jump_short',  'MovementBasic',    'Jump_Full_Short',             False, None,    None),
    ('dodge_back',  'MovementAdvanced', 'Dodge_Backward',              False, None,    None),
    ('walk_back',   'MovementAdvanced', 'Walking_Backwards',           True,  None,    None),
    # -- skeletons
    ('skel_idle',   'Special',          'Skeletons_Idle',              True,  None,    None),
    ('skel_awaken', 'Special',          'Skeletons_Awaken_Standing',   False, None,    None),
    ('skel_rise',   'Special',          'Skeletons_Awaken_Floor',      False, None,    None),
    ('skel_death',  'Special',          'Skeletons_Death',             False, 'death', None),
    ('skel_taunt',  'Special',          'Skeletons_Taunt',             False, None,    None),
]

PACK = 'Rig_Medium_%s.fbx'
T_EPS = 5e-4          # a bone counts as translated when it leaves its bind translation by > 0.5 mm
TIP_LEN = 0.9         # weapon tip distance along the hand slot's +Y (model units; sword blade ~1.4)


def tick_of(frame, fps):
    return int(round(frame * 60.0 / fps))


def detect_events(rule, worlds, names, fps):
    ev = [dmfio.EV_NONE] * 4
    if rule is None:
        return ev, {}
    info = {}
    if rule in ('tip', 'tip_l', 'throw'):
        bone = 'handslot.l' if rule == 'tip_l' else ('hand.r' if rule == 'throw' else 'handslot.r')
        bi = names.index(bone)
        off = np.array([0, TIP_LEN if rule != 'throw' else 0.0, 0, 1.0])
        p = np.array([(W[bi] @ off)[:3] for W in worlds])
        v = np.zeros(len(p))
        v[1:-1] = np.linalg.norm(p[2:] - p[:-2], axis=1) * 0.5 * fps
        peak = int(np.argmax(v))
        info = dict(bone=bone, peak_frame=peak, peak_speed=float(v[peak]))
        if rule == 'throw':
            ev[1] = tick_of(peak, fps)
        else:
            hit = peak
            for f in range(peak, len(v)):
                if v[f] < 0.35 * v[peak]:
                    hit = f
                    break
            info['impact_frame'] = hit
            ev[0] = tick_of(hit, fps)
    elif rule == 'death':
        # the head's height: the body has hit the ground once the head is within 5 cm of its lowest
        # point (the hips barely drop - the chunky KayKit torso lies on its side at hip height)
        hd = names.index('head')
        y = np.array([W[hd][1, 3] for W in worlds])
        low = int(np.argmin(y))
        for f in range(len(y)):
            if y[f] < y[low] + 0.05:
                low = f
                break
        ev[2] = tick_of(low, fps)
        info = dict(collapse_frame=low, head_low=float(y[low]), head_start=float(y[0]))
    return ev, info


def build(kaykit, out, report):
    import bpy  # noqa: F401  (Blender's Python)
    import rig_sample as rs
    clips, rep = [], {'event_rules': EVENT_DOC, 'clips': []}
    names_ref, rest_ref = None, None
    by_pack = {}
    for c in CLIPS:
        by_pack.setdefault(c[1], []).append(c)
    order = {c[0]: i for i, c in enumerate(CLIPS)}
    built = {}
    for pack, items in by_pack.items():
        arm = rs.import_fbx(os.path.join(kaykit, 'Animations', PACK % pack))
        names = [b.name for b in arm.data.bones]
        rest = rs.rest_locals(arm)
        if names_ref is None:
            names_ref, rest_ref = names, rest
        assert names == names_ref, 'every pack must share the Rig_Medium skeleton'
        assert max(np.abs(rest[i] - rest_ref[i]).max() for i in range(len(names))) < 1e-5, 'rest poses differ'
        for lib_name, _, action, loop, rule, override in items:
            act = bpy.data.actions['Rig_Medium|' + action]
            L, Wm, _, _ = rs.sample_action(arm, act)
            fps = float(bpy.context.scene.render.fps)
            F = len(L)
            if loop:
                F -= 1                                  # the last frame repeats the first
            L = L[:F]
            nb = len(names)
            q = np.array([[rs.mat_to_quat(L[f][i][:3, :3]) for i in range(nb)] for f in range(F)])
            for f in range(1, F):                       # hemisphere continuity (nlerp takes the short way anyway)
                flip = (q[f] * q[f - 1]).sum(-1) < 0
                q[f][flip] *= -1
            t_all = np.array([[L[f][i][:3, 3] for i in range(nb)] for f in range(F)])
            bind_t = np.array([rest_ref[i][:3, 3] for i in range(nb)])
            moved = [i for i in range(nb) if names[i] != 'root' and np.abs(t_all[:, i] - bind_t[i]).max() > T_EPS]
            hips = names.index('hips')
            if hips not in moved:
                moved.append(hips)
            moved.sort()
            # world matrices with the root folded into the hips (the runtime's pose chain)
            worlds = []
            for f in range(F):
                W = []
                for i in range(nb):
                    Lf = np.eye(4) if names[i] == 'root' else L[f][i]
                    p = arm.data.bones[i].parent
                    W.append(Lf if p is None else W[names.index(p.name)] @ Lf)
                worlds.append(W)
            ev, info = detect_events(rule, worlds, names, fps)
            if override:
                ev = list(override) + [dmfio.EV_NONE] * (4 - len(override))
            joints = np.array([[W[i][:3, 3] for i in range(nb)] for W in worlds]).reshape(-1, 3)
            lo, hi = joints.min(0), joints.max(0)
            centre = (lo + hi) * 0.5
            radius = float(np.linalg.norm(joints - centre, axis=1).max())
            built[lib_name] = dict(name=lib_name, loop=loop, fps=fps, t_bones=moved, t=t_all[:, moved, :], q=q, events=ev,
                                   bound=[float(centre[0]), float(centre[1]), float(centre[2]), radius])
            rep['clips'].append(dict(name=lib_name, source=action, pack=pack, frames=F, seconds=round(F / fps, 3), loop=loop,
                                     translated=[names[i] for i in moved], events=ev, detect=info,
                                     bound=[round(x, 3) for x in built[lib_name]['bound']]))
            print('%-12s %-34s %3d fr  t=%-40s ev=%s' % (lib_name, action, F, ','.join(names[i] for i in moved)[:40], ev))
    clips = [built[c[0]] for c in sorted(CLIPS, key=lambda c: order[c[0]])]
    data = dmfio.write_dma(names_ref, clips)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'wb').write(data)
    rep['bytes'] = len(data)
    rep['bones'] = names_ref
    rep['clips'].sort(key=lambda c: order[c['name']])
    if report:
        os.makedirs(os.path.dirname(os.path.abspath(report)), exist_ok=True)
        json.dump(rep, open(report, 'w'), indent=1)
    print('wrote %s: %d clips, %d bytes' % (out, len(clips), len(data)))


def check(kaykit, romdisk):
    """The convention check: re-sampling the clips the field models were built from reproduces
    their stored keys (within the int16 quantisation)."""
    import bpy  # noqa: F401
    import rig_sample as rs
    arm = rs.import_fbx(os.path.join(kaykit, 'Animations', PACK % 'Special'))
    m = dmfio.read_dmf(os.path.join(romdisk, 'models', 'skel_warrior.dmf'))
    worst = 0.0
    for dmf_name, action in (('idle', 'Skeletons_Idle'), ('walk', 'Skeletons_Walking')):
        L, _, names, _ = rs.sample_action(arm, bpy.data.actions['Rig_Medium|' + action])
        a = [x for x in m['anims'] if x['name'] == dmf_name][0]
        for f in range(a['frames']):
            for i in range(len(names)):
                qq = rs.mat_to_quat(L[f][i][:3, :3])
                worst = max(worst, min(np.abs(qq - a['q'][f][i]).max(), np.abs(qq + a['q'][f][i]).max()))
    ok = worst < 1.0 / 32767
    print('convention check: worst quaternion difference %.2e (int16 step %.2e): %s' % (worst, 1 / 32767, 'OK' if ok else 'FAIL'))
    return ok


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
    ap.add_argument('--kaykit', default=os.path.join(root, '..', 'KayKit'))
    ap.add_argument('--out', default=os.path.join(root, 'romdisk', 'anims', 'rig_medium.dma'))
    ap.add_argument('--report', default=os.path.join(root, 'tools', 'battle3d', 'reports', 'anims.json'))
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:])
    if a.check:
        sys.exit(0 if check(a.kaykit, os.path.join(root, 'romdisk')) else 1)
    build(a.kaykit, a.out, a.report)
