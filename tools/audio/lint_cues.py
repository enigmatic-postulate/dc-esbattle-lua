"""lint_cues.py - checks romdisk/audio/cues.txt, its files and the budgets (slice 3D-7, plan 6.1, 8; tests AU-T1,
AU-T4). Python 3 standard library. The parse rules and messages are the C++ loader's (src/audio.cpp parse_table);
tests/sound_save runs both over the same fixtures.

    python3 tools/audio/lint_cues.py                 # the repository's table, files, events and budgets
    python3 tools/audio/lint_cues.py --table FILE    # parse one table only: prints its error ("" if fine)
"""
import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adpcm  # noqa: E402
from make_standins import STANDINS  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MAX_SFX, MAX_MUSIC, MAX_ALIAS, MAX_SILENT = 48, 12, 24, 16
SFX_BUDGET = 320 * 1024                 # sound RAM for the effects (plan 7.1)
SOUND_RAM_RESERVE = 128 * 1024          # the AICA driver + the stream buffers (plan 8)
SOUND_RAM_BUDGET = 1024 * 1024
MUSIC_BUDGET = 2 * 1024 * 1024
AUDIO_BUDGET = int(2.4 * 1024 * 1024)
ROMDISK_BUDGET = 5500000
NAME = re.compile(r'^[a-z0-9_]{1,23}$')


class Err(Exception):
    pass


def to_int(s, lo, hi):
    return int(s) if re.match(r'^[+-]?\d+$', s) and lo <= int(s) <= hi else None


def parse(text):
    sfx, music, alias, silent = {}, {}, {}, []
    for ln, line in enumerate(text.split('\n'), 1):
        tok = line.split('#', 1)[0].split()[:8]
        if not tok:
            continue

        def fail(msg):
            raise Err('line %d: %s' % (ln, msg[:119]))
        if len(tok) < 2:
            fail("'%s' needs a name" % tok[0])
        name = tok[1]
        if not NAME.match(name):
            fail("bad name '%s' (a-z, 0-9, _; at most 23)" % name[:30])
        if name in sfx or name in music or name in alias or name in silent:
            fail("'%s' is defined twice" % name)
        if tok[0] == 'music':
            if len(tok) not in (5, 7):
                fail('music: expected `music <name> loop|once <file> <vol> [<fade_out> <fade_in>]`')
            if tok[2] not in ('loop', 'once'):
                fail("music '%s': '%s' is not loop or once" % (name, tok[2][:20]))
            if len(tok[3]) > 55:
                fail("music '%s': file name too long" % name)
            vol = to_int(tok[4], 0, 255)
            if vol is None:
                fail("music '%s': vol '%s' is not 0-255" % (name, tok[4][:12]))
            if len(tok) == 7 and (to_int(tok[5], 0, 600) is None or to_int(tok[6], 0, 600) is None):
                fail("music '%s': fades '%s' '%s' are not 0-600 ticks" % (name, tok[5][:12], tok[6][:12]))
            if len(music) >= MAX_MUSIC:
                fail('more than %d music cues' % MAX_MUSIC)
            music[name] = {'file': tok[3], 'vol': vol, 'once': tok[2] == 'once'}
        elif tok[0] == 'sfx':
            if len(tok) not in (6, 7):
                fail('sfx: expected `sfx <name> <file> <vol> <prio> <min_gap> [pan_actor]`')
            if len(tok[2]) > 55:
                fail("sfx '%s': file name too long" % name)
            vol, prio, gap = to_int(tok[3], 0, 255), to_int(tok[4], 1, 9), to_int(tok[5], 0, 600)
            if vol is None:
                fail("sfx '%s': vol '%s' is not 0-255" % (name, tok[3][:12]))
            if prio is None:
                fail("sfx '%s': prio '%s' is not 1-9" % (name, tok[4][:12]))
            if gap is None:
                fail("sfx '%s': min_gap '%s' is not 0-600" % (name, tok[5][:12]))
            if len(tok) == 7 and tok[6] != 'pan_actor':
                fail("sfx '%s': unknown flag '%s'" % (name, tok[6][:20]))
            if len(sfx) >= MAX_SFX:
                fail('more than %d sfx cues' % MAX_SFX)
            sfx[name] = {'file': tok[2], 'vol': vol, 'prio': prio, 'gap': gap}
        elif tok[0] == 'alias':
            if len(tok) != 3:
                fail('alias: expected `alias <event> <cue>`')
            if tok[2] not in sfx and tok[2] not in music:
                fail("alias '%s': no cue '%s' above it" % (name, tok[2][:30]))
            if len(alias) >= MAX_ALIAS:
                fail('more than %d aliases' % MAX_ALIAS)
            alias[name] = tok[2]
        elif tok[0] == 'silent':
            if len(tok) != 2:
                fail('silent: expected `silent <event>`')
            if len(silent) >= MAX_SILENT:
                fail('more than %d silent events' % MAX_SILENT)
            silent.append(name)
        else:
            fail("unknown line type '%s' (music, sfx, alias, silent)" % tok[0][:20])
    return sfx, music, alias, silent


def events_in_source():
    """Event names the game fires: string literals on eng::audio_event lines and in SOUND[] tables (src/), and the
    scripts' `sound <cue>` / `music <cue>` commands (Stage B)."""
    ev = {}
    for p in sorted(glob.glob(os.path.join(ROOT, 'src', '*.cpp'))):
        for ln, line in enumerate(open(p, encoding='utf-8', errors='replace'), 1):
            code = line.split('//', 1)[0]
            if 'eng::audio_event(' in code or re.search(r'\bSOUND\[\]\s*=', code):
                code = re.sub(r'\b(?!audio_event\b)\w+\("[^"]*"\)', '', code)     # other calls' literals: find("barrow")
                for name in re.findall(r'"([a-z0-9_]+)"', code):
                    ev.setdefault(name, '%s:%d' % (os.path.relpath(p, ROOT), ln))
    for p in sorted(glob.glob(os.path.join(ROOT, 'romdisk', 'scripts', '*.txt'))):
        for ln, line in enumerate(open(p, encoding='utf-8', errors='replace'), 1):
            m = re.match(r'\s*(sound|music)\s+([a-z0-9_]+)', line.split('#', 1)[0])
            if m and m.group(2) != 'stop':
                ev.setdefault(('music_' if m.group(1) == 'music' else '') + m.group(2), '%s:%d' % (os.path.relpath(p, ROOT), ln))
    return ev


def resolves(name, sfx, music, alias, silent):
    if name in alias or name in silent:
        return True
    if name.startswith('music_'):
        return name[6:] in music and not music[name[6:]]['once']
    if name.startswith('sting_'):
        return name[6:] in music and music[name[6:]]['once']
    return name in sfx


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--table')
    args = ap.parse_args()
    if args.table:
        try:
            parse(open(args.table).read())
            print('')
        except Err as e:
            print(e)
        return 0
    audio = os.path.join(ROOT, 'romdisk', 'audio')
    errs = []
    try:
        sfx, music, alias, silent = parse(open(os.path.join(audio, 'cues.txt')).read())
    except Err as e:
        print('cues.txt %s' % e)
        return 1
    sizes = {'sfx': 0, 'music': 0}
    listed = set()
    for kind, table in (('sfx', sfx), ('music', music)):
        for name, c in sorted(table.items()):
            path = os.path.join(audio, c['file'])
            listed.add(c['file'])
            try:
                info, pcm, s0, s1 = adpcm.decode_adp(open(path, 'rb').read())
            except (OSError, ValueError) as e:
                errs.append('%s %s: %s' % (kind, name, e))
                continue
            sizes[kind] += len(info['data'])
            if kind == 'sfx' and info['samples'] > 65534:
                errs.append('sfx %s: %d samples; an effect holds at most 65534' % (name, info['samples']))
            if kind == 'music':
                looped = info['loop_start'] < info['samples']
                if c['once'] == looped:
                    errs.append('music %s: the table says %s but the file %s loop points' % (name, 'once' if c['once'] else 'loop', 'has' if looped else 'has no'))
                if looped and not (s0 == s1 == adpcm.RESET) and info['format'] == adpcm.FMT_ADPCM:
                    errs.append('music %s: the loop seam is not a reset seam (decoder %r at the loop start, %r at its end)' % (name, s0, s1))
            if c['file'] not in STANDINS and not sourced(c['file']):
                errs.append('%s %s: %s is neither a stand-in nor listed in assets/audio/SOURCES.md' % (kind, name, c['file']))
    for p in sorted(glob.glob(os.path.join(audio, '*', '*.adp'))):
        rel = os.path.relpath(p, audio).replace(os.sep, '/')
        if rel not in listed:
            errs.append('%s is on the romdisk but no cue plays it' % rel)
    ev = events_in_source()
    for name, where in sorted(ev.items()):
        if not resolves(name, sfx, music, alias, silent):
            errs.append("event '%s' (%s) has no cue, alias or `silent` line" % (name, where))
    romdisk = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(os.path.join(ROOT, 'romdisk')) for f in fs)
    plan = sizes['sfx'] + SOUND_RAM_RESERVE
    for what, got, cap in (('effects', sizes['sfx'], SFX_BUDGET), ('sound RAM plan (effects + 128 KiB driver and stream)', plan, SOUND_RAM_BUDGET),
                           ('music', sizes['music'], MUSIC_BUDGET), ('audio on the romdisk', sizes['sfx'] + sizes['music'], AUDIO_BUDGET),
                           ('romdisk', romdisk, ROMDISK_BUDGET)):
        if got > cap:
            errs.append('budget: %s %d bytes > %d' % (what, got, cap))
    if errs:
        print('\n'.join(errs))
        return 1
    print('cues ok: %d effects (%d KiB of %d), %d tracks (%d KiB of %d), %d aliases, %d events in the code and scripts all resolve; '
          'sound RAM plan %d KiB of %d, romdisk %.2f MB of %.1f' % (
              len(sfx), sizes['sfx'] // 1024, SFX_BUDGET // 1024, len(music), sizes['music'] // 1024, MUSIC_BUDGET // 1024,
              len(alias), len(ev), plan // 1024, SOUND_RAM_BUDGET // 1024, romdisk / 1e6, ROMDISK_BUDGET / 1e6))
    return 0


def sourced(rel):
    p = os.path.join(ROOT, 'assets', 'audio', 'SOURCES.md')
    return os.path.exists(p) and rel in open(p, encoding='utf-8').read()


if __name__ == '__main__':
    sys.exit(main())
