#!/usr/bin/env python3
"""lint.py - the script compiler in Python, and the GDD 7.4 validation (slice 3D-5, task C3).

    python3 tools/dialog/lint.py [romdisk] [--script FILE [--as NAME]] [--dump] [--quiet]

Compiles every script file (romdisk/maps/maps.txt: scripts/<map>.txt for each map, then the `scripts` files) +
speakers.txt exactly as src/script.cpp does (the same pools, the same wrapping with the real font metrics from
romdisk/fonts/dialog.fnt, the same condition byte code, the same actor slots), against every map's entities,
triggers, exits, glows and shots (romdisk/maps/<map>.txt) and the light presets (romdisk/maps/lights.txt), so that
--dump prints the same text as `battle_trace --dump-scripts` (tests/dialog: parity). --script FILE replaces
scripts/<NAME>.txt (default village) - the negative fixtures. Then checks:
  * every label, script and actor reference exists; a choice has 1-3 options; the cancel option is last
  * every flag that is read (conditions, @marker, the map's `if`) is set somewhere (flag, give skill/token,
    or a map entity's `win`)
  * no unreachable page (a page, sys page or choice no path from its script's start reaches)
  * every page fits the box: 3 lines of 420 px with the real font metrics; every choice fits its box
  * every speaker has a name, a colour and its three portraits (romdisk/portraits/<base>_<variant>.dtx)
  * the branch walk: every path ends (no run off the end, no loop with no exit, no loop that never blocks)
  * a cancel ("no") branch shows a page before it ends: it tells the player how to come back (GDD 7.3)
  * the stage only moves forward: `set stage N` where the stage may already be above N is an error
  * slice 3D-6: a script's actors, shots and battles belong to the map it runs on (its @map, then its `map`
    commands); only move, anim, face, emote, cam and wait run in parallel; the credits file exists
Exit status 0 when clean. Errors are printed as file:line: message.
"""
import argparse
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

SC = ['END', 'PAGE', 'SYS', 'CHOICE', 'OPTION', 'JMP', 'JZ', 'CALL', 'STAGE', 'MAXHP', 'FLAG', 'UNFLAG', 'GIVE_POTIONS',
      'TAKE_POTIONS', 'HEAL', 'CHECKPOINT', 'OBJECTIVE', 'FACE', 'ANIM', 'WAIT', 'EMOTE', 'FADE_OUT', 'FADE_IN', 'LETTERBOX',
      'MOVE', 'PLACE', 'CAM', 'PAR', 'BATTLE', 'MAP', 'LIGHT', 'LIGHT_LERP', 'NAMECARD', 'CARD', 'TRANSITION', 'CREDITS',
      'RESULTS', 'TITLE']
(S_MOVE, S_PLACE, S_CAM, S_PAR, S_BATTLE, S_MAP, S_LIGHT, S_LIGHT_LERP, S_NAMECARD, S_CARD, S_TRANSITION, S_CREDITS,
 S_RESULTS, S_TITLE) = range(24, 38)
MAX_MAPS, MAX_ACTORS, MAX_SHOTS, MAX_ENTITIES, MAX_TRIGGERS, MAX_EXITS, MAX_GLOWS, MAX_FILES = 4, 24, 12, 16, 8, 4, 6, 6
MOVE_REL, MOVE_RUN, CAM_FIELD, NAMECARD_TICKS, CARD_TICKS = 1, 2, 0xFF, 120, 150
VARIANTS = ['neutral', 'happy', 'worried']
EMOTES = ['!', '?', '...', 'sweat', 'anger']
DIRS = ['down', 'up', 'left', 'right']
C_PUSH, C_STAGE, C_POTIONS, C_MAXHP, C_FLAG, C_NOT, C_ADD, C_SUB, C_EQ, C_NE, C_LT, C_LE, C_GT, C_GE, C_AND, C_OR = range(1, 17)
PAGE_WIDTH, PAGE_LINES, CHOICE_TEXT_MAX = 420, 3, 300
MAX_OPS, STR_BYTES, COND_BYTES, MAX_SCRIPTS, MAX_LABELS, MAX_FLAGS = 1024, 12 * 1024, 1024, 32, 128, 32
MAX_SPEAKERS, MAX_MARKERS, MAX_OBJECTIVES, COND_MAX = 8, 8, 32, 32
CODES = ['{gold}', '{red}', '{grey}', '{/}']


# ---------------------------------------------------------------------------- font metrics (dialog.fnt)
class Font:
    def __init__(self, path):
        b = open(path, 'rb').read()
        if b[:4] != b'FNT1' or len(b) != 16 + 96 * 8 + 16 * 4:
            raise SystemExit('%s: not a dialog.fnt' % path)
        self.pitch, self.space = b[5], b[10]
        self.adv = {}
        for k in range(96):
            self.adv[chr(32 + k)] = b[16 + 8 * k + 4]

    def advance(self, c):
        return self.adv.get(c, 0) if 32 <= ord(c) < 128 else 0

    def code_len(self, s, i):
        for c in CODES:
            if s.startswith(c, i):
                return len(c)
        return 0

    def width(self, s):
        w, i = 0, 0
        while i < len(s) and s[i] != '\n':
            cl = self.code_len(s, i)
            if cl:
                i += cl
                continue
            w += self.advance(s[i])
            i += 1
        return w

    def glyph_count(self, s):
        k, i = 0, 0
        while i < len(s):
            cl = self.code_len(s, i)
            if cl:
                i += cl
                continue
            if s[i] != '\n':
                k += 1
            i += 1
        return k

    def wrap(self, text, width):
        """src/text.cpp text_wrap: greedy, single spaces, '|' = forced break. None on error."""
        out, lines, line_w, line_empty, i = [], 1, 0, True, 0
        n = len(text)
        while True:
            w0 = i
            while i < n and text[i] not in ' |':
                if text[i] == '{':
                    cl = self.code_len(text, i)
                    if not cl:
                        return None
                    i += cl
                    continue
                if not (33 <= ord(text[i]) <= 126):
                    return None
                i += 1
            word = text[w0:i]
            if not word:
                return None
            ww = self.width(word)
            if ww > width:
                return None
            if not line_empty:
                sp = self.advance(' ')
                if line_w + sp + ww > width:
                    out.append('\n'); lines += 1; line_w = 0
                else:
                    out.append(' '); line_w += sp
            out.append(word); line_w += ww; line_empty = False
            if i < n and text[i] == ' ':
                i += 1
                continue
            if i < n and text[i] == '|':
                i += 1
                out.append('\n'); lines += 1; line_w = 0; line_empty = True
                continue
            break
        return ''.join(out), lines


# ---------------------------------------------------------------------------- maps, lights (slice 3D-6)
class LintError(Exception):
    pass


def next_tok(s, cap):
    """src/script.cpp next_token(): one token (a "quoted string" is one, quotes kept), truncated to cap-1 characters."""
    s = s.lstrip(' \t')
    if not s:
        return '', ''
    if s[0] == '"':
        j = s.find('"', 1)
        body = s[1:j] if j >= 0 else s[1:]
        rest = s[j + 1:] if j >= 0 else ''
        return ('"' + body[:cap - 3] + ('"' if j >= 0 else ''))[:cap], rest
    j = 0
    while j < len(s) and s[j] not in ' \t':
        j += 1
    return s[:j][:cap - 1], s[j:]


def read_lights(path):
    """lights.txt preset names in id order (src/lights.cpp: 'day' first, a redefined name keeps its slot)."""
    names = ['day']
    if os.path.exists(path):
        for ln in open(path).read().split('\n'):
            ln = ln.split('#', 1)[0]
            t = ln.split()
            if len(t) >= 2 and t[0] == 'light' and t[1] not in names:
                names.append(t[1])
    return names


def scan_map(path, fname, maps, lights):
    """src/script.cpp scan_map(): entities, triggers, exits, glows and shots of a map file."""
    m = dict(ents=[], trig=[], exits=[], glows=[], shots=[])
    if not os.path.exists(path) or os.path.getsize(path) >= 16 * 1024:
        raise LintError('%s:0: map file missing or larger than 16 KiB' % fname)
    for n, ln in enumerate(open(path).read().split('\n'), 1):
        body = ln[:199].rstrip('\r ')
        h = body.find(' #')
        if h >= 0:
            body = body[:h]
        cond, has_if = '', False
        c = body.find(' if ')
        if c >= 0:
            has_if = True
            cond = body[c + 4:].lstrip(' \t')[:63]
            body = body[:c]
        w, r = next_tok(body, 16)
        t = []
        while len(t) < 20:
            tok, r = next_tok(r, 16)
            if not tok:
                break
            t.append(tok)
        fail = lambda msg, d=None: (_ for _ in ()).throw(LintError('%s:%d: %s%s' % (fname, n, msg, (': ' + d) if d else '')))
        if w == 'entity' and len(t) >= 6:
            if len(m['ents']) >= MAX_ENTITIES:
                fail('too many entities')
            e = dict(id=t[0], kind=t[1], look=t[2], talk='', win='', cond=cond if has_if else '')
            if t[1] == 'npc' and len(t) >= 8 and t[6] == 'talk':
                e['talk'] = t[7]
            for k in range(6, len(t) - 1):
                if t[k] == 'win':
                    e['win'] = t[k + 1]
            m['ents'].append(e)
        elif w == 'trigger' and len(t) >= 6:
            if len(m['trig']) >= MAX_TRIGGERS:
                fail('too many triggers')
            m['trig'].append(dict(script=t[5], cond=cond if has_if else '', line=n))
        elif w == 'exit' and len(t) >= 9:
            if len(m['exits']) >= MAX_EXITS:
                fail('too many exits')
            if t[5] not in maps:
                fail('exit: the destination is not a map of maps.txt', t[5])
            m['exits'].append(dict(cond=cond if has_if else '', line=n))
        elif w == 'glow' and len(t) >= 4:
            if len(m['glows']) >= MAX_GLOWS:
                fail('too many glows')
            m['glows'].append(dict(cond=cond if has_if else '', line=n))
        elif w == 'shot' and len(t) >= 1:
            if len(m['shots']) >= MAX_SHOTS:
                fail('too many shots')
            m['shots'].append(t[0])
        elif w == 'light' and len(t) >= 1:
            if t[0] not in lights:
                fail('light: unknown preset (lights.txt)', t[0])
    return m


def read_maps_txt(romdisk, current='village'):
    """maps.txt: the maps (id order) and the extra script files; a map not listed makes a one-map table."""
    maps, extra = [], []
    p = os.path.join(romdisk, 'maps', 'maps.txt')
    if os.path.exists(p):
        for n, ln in enumerate(open(p).read().split('\n'), 1):
            ln = ln.split('#', 1)[0]
            t = ln.split()
            if not t:
                continue
            if len(t) != 2 or len(t[1]) > 15 or t[0] not in ('map', 'scripts'):
                raise LintError('maps.txt:%d: map <name> | scripts <file>' % n)
            if t[0] == 'map':
                if len(maps) >= MAX_MAPS:
                    raise LintError('maps.txt:%d: too many maps (4)' % n)
                if t[1] in maps:
                    raise LintError('maps.txt:%d: duplicate map: %s' % (n, t[1]))
                maps.append(t[1])
            else:
                if len(extra) >= MAX_FILES - MAX_MAPS:
                    raise LintError('maps.txt:%d: too many script files' % n)
                extra.append(t[1])
    if current not in maps:
        maps, extra = [current], []
    return maps, extra


# ---------------------------------------------------------------------------- the compiler (script.cpp)
class Compiler:
    def __init__(self, font, clips, speakers_path, maps, scans, lights):
        self.font, self.clips, self.fname = font, clips, 'speakers.txt'
        self.maps, self.scans, self.lights = maps, scans, lights
        self.actors = ['rowan']       # actor slots (script.cpp actor_find)
        self.file_map = self.cur_map = -1
        self.ops = []                 # dicts: code a b c str + line
        self.strs = bytearray()
        self.cond = bytearray()
        self.scripts = []             # name, first, end
        self.speakers = []            # id, name, rgb, portrait
        self.markers = []             # entity, kind, cond_off, cond_len
        self.objectives = [None]      # string offsets; [0] = none
        self.flags = []
        self.labels = {}              # name -> (pc, line)
        self.fix = []                 # (op, name, call, line)
        self.ifs = []
        self.line = 0
        self.file = self.fname
        self.cond_src = {}            # cond offset -> (source, line) for the analyses
        self.flag_reads = {}          # flag -> first line read
        self.flag_read_file = {}      # flag -> the file of that line
        self.flag_sets = set()
        self.ent_cond = {}
        self.load_speakers(speakers_path)

    def fail(self, msg, detail=None):
        raise LintError('%s:%d: %s%s' % (self.file, self.line, msg, (': ' + detail) if detail else ''))

    # -- helpers
    def add_str(self, s):
        b = s.encode('latin-1')
        if len(self.strs) + len(b) + 1 > STR_BYTES:
            self.fail('string pool full (12 KiB)')
        off = len(self.strs)
        self.strs += b + b'\0'
        return off

    def str_at(self, off):
        e = self.strs.index(b'\0', off)
        return self.strs[off:e].decode('latin-1')

    def emit(self, code, a=0, b=0, c=0, s=0):
        if len(self.ops) >= MAX_OPS:
            self.fail('op pool full (1024)')
        self.ops.append(dict(code=code, a=a & 0xFF, b=b & 0xFFFF, c=c & 0xFFFF, str=s & 0xFFFF, line=self.line, file=self.fname))
        return len(self.ops) - 1

    def flag_find(self, name):
        return self.flags.index(name) if name in self.flags else -1

    def scan_entity(self, m, name):
        if m < 0 or m >= len(self.maps):
            return -1
        for i, e in enumerate(self.scans[m]['ents']):
            if e['id'] == name:
                return i
        return -1

    def actor_find(self, name):
        """an actor of the current line's map -> its slot (added on first use); -1 unknown; another map's: an error"""
        if name == 'rowan':
            return 0
        if self.scan_entity(self.cur_map, name) < 0:
            for m in range(len(self.maps)):
                if self.scan_entity(m, name) >= 0:
                    self.fail('actor not on the script\'s map', '%s (on %s, the line runs on %s)' % (
                        name, self.maps[m], self.maps[self.cur_map] if self.cur_map >= 0 else 'no map'))
            return -1
        if name in self.actors[1:]:
            return self.actors.index(name, 1)
        if len(self.actors) >= MAX_ACTORS:
            self.fail('too many actors (24)')
        self.actors.append(name[:15])
        return len(self.actors) - 1

    def actor_name(self, a):
        return self.actors[a] if 0 <= a < len(self.actors) else '?'

    def light_find(self, name):
        return self.lights.index(name) if name in self.lights else -1

    def light_name(self, i):
        return self.lights[i] if 0 <= i < len(self.lights) else self.lights[0]

    # -- conditions: the same shunting-yard as compile_cond()
    PREC = {C_NOT: 6, C_ADD: 5, C_SUB: 5, C_LT: 4, C_LE: 4, C_GT: 4, C_GE: 4, C_EQ: 3, C_NE: 3, C_AND: 2, C_OR: 1}

    def compile_cond(self, s):
        out, depth, stack, expect = [], 0, [], True
        src = s

        def put(*bs):
            for b in bs:
                if len(out) >= COND_MAX:
                    self.fail('condition longer than 32 bytes')
                out.append(b)

        def pop():
            nonlocal depth
            op = stack.pop()
            if op == C_NOT:
                if depth < 1:
                    self.fail('bad condition')
            else:
                if depth < 2:
                    self.fail('bad condition')
                depth -= 1
            put(op)

        s = s.lstrip(' \t')
        if not s:
            self.fail('empty condition')
        i = 0
        while i < len(s):
            while i < len(s) and s[i] in ' \t':
                i += 1
            if i >= len(s):
                break
            ch = s[i]
            if ch.isdigit() or ('a' <= ch <= 'z') or ch == '_':
                if not expect:
                    self.fail('bad condition: two operands in a row')
                j = i
                while j < len(s) and (s[j].isdigit() or 'a' <= s[j] <= 'z' or s[j] == '_'):
                    j += 1
                w = s[i:j][:23]
                i = j
                if w[0].isdigit():
                    if not w.isdigit() or int(w) > 255:
                        self.fail('bad number in condition (0..255)', w)
                    put(C_PUSH, int(w))
                elif w == 'stage':
                    put(C_STAGE)
                elif w == 'potions':
                    put(C_POTIONS)
                elif w == 'max_hp':
                    put(C_MAXHP)
                else:
                    f = self.flag_find(w)
                    if f < 0:
                        self.fail('unknown name in condition (not stage, potions, max_hp or a @flags flag)', w)
                    put(C_FLAG, f)
                    self.flag_reads.setdefault(w, self.line)
                    self.flag_read_file.setdefault(w, self.file)
                depth += 1
                expect = False
                continue
            if ch == '(':
                if not expect:
                    self.fail("bad condition: '(' after an operand")
                stack.append(-1); i += 1
                continue
            if ch == ')':
                if expect:
                    self.fail("bad condition: ')' without an operand")
                while stack and stack[-1] != -1:
                    pop()
                if not stack:
                    self.fail("bad condition: unbalanced ')'")
                stack.pop(); i += 1
                continue
            if ch == '!' and s[i + 1:i + 2] != '=':
                if not expect:
                    self.fail("bad condition: '!' after an operand")
                stack.append(C_NOT); i += 1
                continue
            two = s[i:i + 2]
            op, adv = {'==': (C_EQ, 2), '!=': (C_NE, 2), '<=': (C_LE, 2), '>=': (C_GE, 2), '&&': (C_AND, 2),
                       '||': (C_OR, 2)}.get(two, (None, 1))
            if op is None:
                op = {'<': C_LT, '>': C_GT, '+': C_ADD, '-': C_SUB}.get(ch)
            if op is None:
                self.fail('bad character in condition', ch)
            if expect:
                self.fail('bad condition: an operator where an operand belongs')
            while stack and stack[-1] != -1 and self.PREC[stack[-1]] >= self.PREC[op]:
                pop()
            stack.append(op)
            i += adv
            expect = True
        if expect:
            self.fail('bad condition: it ends with an operator')
        while stack:
            if stack[-1] == -1:
                self.fail("bad condition: unbalanced '('")
            pop()
        if depth != 1:
            self.fail('bad condition')
        if len(self.cond) + len(out) > COND_BYTES:
            self.fail('condition pool full (1 KiB)')
        off = len(self.cond)
        self.cond += bytes(out)
        self.cond_src[off] = (src.strip(), self.line)
        return off, len(out)

    # -- speakers.txt
    def load_speakers(self, path):
        self.file = 'speakers.txt'
        if not os.path.exists(path):
            self.line = 0
            self.fail('missing speakers.txt')
        for n, ln in enumerate(split_lines(open(path).read()), 1):
            self.line = n
            t = tokens(ln)
            if not t:
                continue
            if len(t) != 5 or t[0] != '@speaker' or len(t[2]) < 3 or t[2][0] != '"' or t[2][-1] != '"' or len(t[3]) != 6:
                self.fail('@speaker id "Name" RRGGBB portrait_base')
            sid, name, col, por = t[1], t[2][1:-1], t[3], t[4]
            if len(sid) > 11 or len(name) > 11 or len(por) > 11:
                self.fail('speaker id, name and portrait base are 11 characters at most')
            if sid == 'sys':
                self.fail("'sys' is reserved")
            if any(s['id'] == sid for s in self.speakers):
                self.fail('duplicate speaker', sid)
            if len(self.speakers) >= MAX_SPEAKERS:
                self.fail('too many speakers (8)')
            try:
                rgb = int(col, 16)
            except ValueError:
                self.fail('bad colour', col)
            self.speakers.append(dict(id=sid, name=name, rgb=rgb, portrait=por, line=n))
        self.file = self.fname

    # -- statements
    def in_script(self):
        return self.scripts and self.scripts[-1]['end'] is None

    def close_script(self):
        if not self.in_script():
            return
        if self.ifs:
            self.line = self.ifs[-1]['line']
            self.fail('@if without @end')
        self.scripts[-1]['end'] = len(self.ops)

    def stmt_page(self, line, colon):
        head = line[:colon]
        spk, var = head, ''
        if '(' in head:
            spk, var = head.split('(', 1)
            var = var.rstrip(')')
        text = line[colon + 1:].lstrip(' \t')
        if not text:
            self.fail('empty page')
        r = self.font.wrap(text, PAGE_WIDTH)
        if r is None or len(r[0]) >= 511:
            self.fail("page text cannot be wrapped (a word wider than 420 px, a double space, a space next to '|', or a character the font lacks)")
        wrapped, lines = r
        if lines > PAGE_LINES:
            self.fail('page does not fit the box', '%d lines of 420 px, at most 3' % lines)
        off = self.add_str(wrapped)
        glyphs = self.font.glyph_count(wrapped)
        if spk == 'sys':
            if var:
                self.fail('sys: pages have no variant')
            self.emit(2, 0, 0, glyphs, off)
            return
        si = next((k for k, s in enumerate(self.speakers) if s['id'] == spk), -1)
        if si < 0:
            self.fail('unknown speaker (not in speakers.txt)', spk)
        vi = 0
        if var:
            if var not in VARIANTS:
                self.fail('unknown portrait variant (neutral, happy, worried)', var)
            vi = VARIANTS.index(var)
        self.used_portraits.add((si, vi))
        self.emit(1, si, vi, glyphs, off)

    def stmt_choice(self, rest):
        opts, q, seg = [], False, ''
        for ch in rest[:319]:
            if ch == '"':
                q = not q
            if ch == '|' and not q:
                opts.append(seg); seg = ''
            else:
                seg += ch
        opts.append(seg)
        if len(opts) > 3:
            self.fail('a choice has at most 3 options')
        if len(opts) < 1:
            self.fail('a choice has 1 to 3 options')
        texts, labels, cancel = [], [], 0xFF
        for i, o in enumerate(opts):
            t = tokens(o.strip())
            if t and t[0] == 'cancel':
                if cancel != 0xFF:
                    self.fail('a choice has one cancel option at most')
                if i != len(opts) - 1:
                    self.fail('the cancel option must be the last one (GDD 7.3)')
                cancel = i
                t = t[1:]
            if not t or len(t[0]) < 3 or t[0][0] != '"' or t[0][-1] != '"':
                self.fail('choice option: expected "text" -> label')
            txt = t[0][1:-1]
            w = self.font.wrap(txt, 10000)
            if w is None or w[1] != 1:
                self.fail('bad choice text', txt)
            if self.font.width(txt) > CHOICE_TEXT_MAX:
                self.fail('choice text wider than the choice box', txt)
            texts.append(self.add_str(txt))
            if len(t) < 2 or t[1] != '->':
                self.fail('choice option: expected -> label')
            if len(t) != 3:
                self.fail('choice option: expected one label after ->')
            labels.append(t[2])
        c = self.emit(3, len(opts), cancel)
        self.choices.append(dict(pc=c, line=self.line))
        for i in range(len(opts)):
            op = self.emit(4, 0, 0, 0, texts[i])
            self.fix.append((op, labels[i][:15], False, self.line, self.fname))

    def stmt_parallel(self, line):
        parts = line[:511].split('|')
        if len(parts) > 4:
            self.fail('a parallel line has at most 4 commands')
        if len(parts) < 2:
            self.fail('a parallel line needs two commands')
        self.emit(S_PAR, len(parts))
        for p in parts:
            p = p.lstrip(' \t')
            w = tokens(p)[0] if tokens(p) else ''
            if w not in ('move', 'anim', 'face', 'emote', 'cam', 'wait'):
                self.fail('only move, anim, face, emote, cam and wait may run in parallel', w[:23])
            self.stmt_single(p[:199].rstrip(' \t'))

    def stmt_command(self, line):
        t = tokens(line)
        w = t[0]
        if w == 'choice':
            self.stmt_choice(line[len('choice'):].lstrip(' \t'))
            return
        q = False
        for ch in line:
            if ch == '"':
                q = not q
            elif ch == '|' and not q:
                self.stmt_parallel(line)
                return
        self.stmt_single(line)

    def stmt_single(self, line):
        t = tokens(line)
        w = t[0][:23]
        a1 = t[1] if len(t) > 1 else ''
        a2 = t[2] if len(t) > 2 else ''
        more = len(t) > 3

        def num(s, lo, hi):
            return s.isdigit() and lo <= int(s) <= hi
        if w == 'end':
            if a1:
                self.fail('end takes no argument')
            self.emit(0); return
        if w == 'goto':
            if not a1 or a2:
                self.fail('goto takes one label')
            op = self.emit(5); self.fix.append((op, a1[:15], False, self.line, self.fname)); return
        if w == 'call':
            if not a1 or a2:
                self.fail('call takes one script name')
            op = self.emit(7); self.fix.append((op, a1[:15], True, self.line, self.fname)); return
        if w == 'set':
            if more or not num(a2, 0, 255):
                self.fail('set stage|max_hp <0..255>')
            if a1 == 'stage':
                self.emit(8, int(a2)); return
            if a1 == 'max_hp':
                if int(a2) < 1:
                    self.fail('max_hp must be at least 1')
                self.emit(9, int(a2)); return
            self.fail('set stage|max_hp <n>', a1)
        if w in ('flag', 'unflag'):
            if not a1 or a2:
                self.fail('flag|unflag <name>')
            f = self.flag_find(a1)
            if f < 0:
                self.fail('flag not declared in @flags', a1)
            if w == 'flag':
                self.flag_sets.add(a1)
            self.emit(10 if w == 'flag' else 11, f); return
        if w in ('give', 'take'):
            if a1 == 'potions':
                if more or not num(a2, 1, 3):
                    self.fail('give|take potions <1..3>')
                self.emit(12 if w == 'give' else 13, int(a2)); return
            if w == 'give' and a1 == 'skill':
                if a2 != 'fireball' or more:
                    self.fail('give skill fireball (the one learnable skill)')
                f = self.flag_find('skill_fire')
                if f < 0:
                    self.fail('give skill fireball needs the flag skill_fire in @flags')
                self.flag_sets.add('skill_fire')
                self.emit(10, f); return
            if w == 'give' and a1 == 'token':
                if a2:
                    self.fail('give token takes nothing else')
                f = self.flag_find('has_token')
                if f < 0:
                    self.fail('give token needs the flag has_token in @flags')
                self.flag_sets.add('has_token')
                self.emit(10, f); return
            self.fail('give potions N | take potions N | give skill fireball | give token')
        if w == 'heal':
            if a1:
                self.fail('heal takes no argument')
            self.emit(14); return
        if w == 'checkpoint':
            ids = {'c1': 1, 'c2': 2, 'c3': 3, 'rest': 9}
            if a1 and a1 not in ids:
                self.fail('checkpoint [c1|c2|c3|rest]', a1)
            if a2:
                self.fail('checkpoint takes one id at most')
            self.emit(15, ids.get(a1, 1)); return
        if w == 'objective':
            s = line[len('objective'):].strip()
            if len(s) < 2 or s[0] != '"' or s[-1] != '"':
                self.fail('objective "text"')
            txt = s[1:-1]
            if len(txt) >= 80:
                self.fail('objective text too long')
            if '"' in txt:
                self.fail('objective "text"')
            oid = 0
            if txt:
                i = txt.find('{')
                while i >= 0:
                    if not txt.startswith('{patrols}', i):
                        self.fail('objective: the only placeholder is {patrols}')
                    i = txt.find('{', i + 1)
                for k in range(1, len(self.objectives)):
                    if self.str_at(self.objectives[k]) == txt:
                        oid = k
                if not oid:
                    if len(self.objectives) >= MAX_OBJECTIVES:
                        self.fail('too many objectives (32)')
                    self.objectives.append(self.add_str(txt))
                    oid = len(self.objectives) - 1
                if self.font.width(txt.replace('{patrols}', '0')) > 560:
                    self.fail('objective wider than the HUD plate (560 px)', txt)
            self.emit(16, oid); return
        if w == 'face':
            a = self.actor_find(a1)
            if a < 0:
                self.fail('face: unknown actor (rowan or a map entity id)', a1)
            if more or not a2:
                self.fail('face <actor> <down|up|left|right|actor>')
            if a2 in DIRS:
                self.emit(17, a, DIRS.index(a2)); return
            tgt = self.actor_find(a2)
            if tgt < 0:
                self.fail('face: unknown direction or actor', a2)
            if tgt == a:
                self.fail('face: an actor cannot face itself')
            self.emit(17, a, 0x100 | tgt); return
        if w == 'anim':
            a = self.actor_find(a1)
            if a < 0:
                self.fail('anim: unknown actor (rowan or a map entity id)', a1)
            if more or not a2:
                self.fail('anim <actor> <clip>')
            if a2 not in self.clips:
                self.fail('anim: no such clip in the animation library', a2)
            self.emit(18, a, self.clips.index(a2)); return
        if w == 'emote':
            a = self.actor_find(a1)
            if a < 0:
                self.fail('emote: unknown actor', a1)
            if more:
                self.fail('emote <actor> <!|?|...|sweat|anger>')
            if a2 not in EMOTES:
                self.fail('emote kind must be ! ? ... sweat anger', a2)
            self.emit(20, a, EMOTES.index(a2)); return
        if w == 'wait':
            if a2 or not num(a1, 1, 600):
                self.fail('wait <1..600 ticks>')
            self.emit(19, 0, int(a1)); return
        if w == 'fade':
            if more or a1 not in ('out', 'in') or not num(a2, 1, 240):
                self.fail('fade out|in <1..240 ticks>')
            self.emit(21 if a1 == 'out' else 22, 0, int(a2)); return
        if w == 'letterbox':
            if a2 or a1 not in ('on', 'off'):
                self.fail('letterbox on|off')
            self.emit(23, 1 if a1 == 'on' else 0); return
        # ---- slice 3D-6: cutscene commands (script.cpp stmt_single)
        a3 = t[3] if len(t) > 3 else ''
        a4 = t[4] if len(t) > 4 else ''

        def sint(v, lo, hi):
            sign = v[:1] in ('-', '+')
            d = v[1:] if sign else v
            if not d.isdigit() or int(d) > 100000:
                return None
            n = -int(d) if v[:1] == '-' else int(d)
            return (n, sign) if lo <= n <= hi else None
        if w == 'move':
            a = self.actor_find(a1)
            if a < 0:
                self.fail('move: unknown actor (rowan or a map entity id)', a1)
            x, y = sint(a2, -63, 63), sint(a3, -63, 63)
            if x is None or y is None or (a4 and a4 != 'run') or len(t) > 5:
                self.fail('move <actor> <x> <y> [run] (a signed dx dy: relative)')
            rel = x[1] or y[1]
            if not rel and (x[0] < 0 or y[0] < 0):
                self.fail('move: a tile is 0..63')
            self.emit(S_MOVE, a, (x[0] & 0xFF) | (y[0] & 0xFF) << 8, (MOVE_REL if rel else 0) | (MOVE_RUN if a4 else 0)); return
        if w == 'place':
            a = self.actor_find(a1)
            if a < 0:
                self.fail('place: unknown actor (rowan or a map entity id)', a1)
            d = DIRS.index(a4) if a4 in DIRS else (0 if not a4 else -1)
            if not num(a2, 0, 63) or not num(a3, 0, 63) or d < 0 or len(t) > 5:
                self.fail('place <actor> <x> <y> [down|up|left|right]')
            self.emit(S_PLACE, a, int(a2) | int(a3) << 8, d); return
        if w == 'cam':
            if more or (a2 and not num(a2, 0, 600)):
                self.fail('cam <shot|field> [0..600 ticks]')
            tk = int(a2) if a2 else 0
            if a1 == 'field':
                self.emit(S_CAM, CAM_FIELD, tk, max(self.cur_map, 0)); return
            shots = self.scans[self.cur_map]['shots'] if self.cur_map >= 0 else []
            if a1 not in shots:
                self.fail('cam: unknown shot (not a `shot` of the script\'s map)', a1)
            self.emit(S_CAM, shots.index(a1), tk, self.cur_map); return
        if w == 'battle':
            a = self.actor_find(a1)
            if a <= 0:
                self.fail('battle: not an enemy of the script\'s map', a1)
            e = self.scan_entity(self.cur_map, a1)
            if e < 0 or self.scans[self.cur_map]['ents'][e]['kind'] == 'npc':
                self.fail('battle: not an enemy of the script\'s map', a1)
            if more or (a2 and a2 != 'boss'):
                self.fail('battle <enemy> [boss]')
            self.emit(S_BATTLE, a, 1 if a2 else 0); return
        if w == 'map':
            a5 = t[5] if len(t) > 5 else ''
            a6 = t[6] if len(t) > 6 else ''
            if a1 not in self.maps:
                self.fail('map: not a map of maps.txt', a1)
            m = self.maps.index(a1)
            if not num(a2, 0, 63) or not num(a3, 0, 63) or a4 not in DIRS or len(t) > 7 or (a5 and (a5 != 'light' or not a6)):
                self.fail('map <name> <x> <y> <dir> [light <preset>]')
            preset = -1
            if a5:
                preset = self.light_find(a6)
                if preset < 0:
                    self.fail('map: unknown light preset (lights.txt)', a6)
            self.cur_map = m
            self.emit(S_MAP, m, int(a2) | int(a3) << 8, DIRS.index(a4) | (preset + 1) << 4); return
        if w == 'light':
            if a1 == 'lerp':
                fr, to = self.light_find(a2), self.light_find(a3)
                if fr < 0 or to < 0 or not num(a4, 1, 3600) or len(t) > 5:
                    self.fail('light lerp <preset> <preset> <1..3600 ticks> (presets of lights.txt)')
                self.emit(S_LIGHT_LERP, fr, int(a4), to); return
            l = self.light_find(a1)
            if l < 0 or a2:
                self.fail('light <preset> | light lerp <a> <b> <ticks> (presets of lights.txt)', a1)
            self.emit(S_LIGHT, l); return
        if w in ('namecard', 'card'):
            q = line[len(w):].lstrip(' \t')
            if len(q) < 2 or q[0] != '"' or q[-1] != '"' or len(q) - 2 >= 48 or '"' in q[1:-1] or len(q) == 2:
                self.fail('namecard|card "text" (1..47 characters)')
            txt = q[1:-1]
            r = self.font.wrap(txt, 560)
            if r is None or r[1] != 1:
                self.fail('card text must fit one line of 560 px', txt)
            off = self.add_str(txt)
            self.emit(S_NAMECARD if w == 'namecard' else S_CARD, 0, NAMECARD_TICKS if w == 'namecard' else CARD_TICKS, 0, off); return
        if w == 'transition':
            if a2 or a1[:1] != 'T' or not num(a1[1:], 1, 11):
                self.fail('transition T1..T11')
            self.emit(S_TRANSITION, int(a1[1:])); return
        if w in ('credits', 'results', 'title'):
            if a1:
                self.fail('credits, results and title take no argument')
            self.emit({'credits': S_CREDITS, 'results': S_RESULTS, 'title': S_TITLE}[w]); return
        self.fail('unknown command', w)

    def stmt(self, line):
        if line[0] == '@':
            t = tokens(line)
            w = t[0][:15]
            rest = line[len(t[0]):].lstrip(' \t')
            if w == '@flags':
                return
            if w == '@speaker':
                self.fail('@speaker belongs in speakers.txt')
            if w == '@map':
                if self.in_script():
                    self.close_script()
                name = t[1] if len(t) > 1 else ''
                if name not in self.maps or len(t) > 2:
                    self.fail('@map <a map of maps.txt>', name)
                self.file_map = self.cur_map = self.maps.index(name)
                return
            if w == '@marker':
                if self.in_script():
                    self.fail('@marker inside a script')
                eid = t[1] if len(t) > 1 else ''
                kind = t[2] if len(t) > 2 else ''
                self.cur_map = self.file_map
                e = self.scan_entity(self.file_map, eid)
                if e < 0 or self.scans[self.file_map]['ents'][e]['kind'] != 'npc':
                    self.fail('@marker: not an npc of the map', eid)
                if kind not in ('!', '...'):
                    self.fail('@marker kind must be ! or ...', kind)
                if len(self.markers) >= MAX_MARKERS:
                    self.fail('too many markers (8)')
                crest = rest[len(eid):].lstrip(' \t')[len(kind):]
                off, ln = self.compile_cond(crest)
                self.markers.append(dict(entity=e, kind=0 if kind == '!' else 1, cond=off, len=ln, map=self.file_map))
                return
            if w == '@script':
                self.close_script()
                name = t[1] if len(t) > 1 else ''
                opt = t[2] if len(t) > 2 else ''
                if not name or len(t) > 3 or len(name) > 15 or (opt and opt != 'skip'):
                    self.fail('@script <name> [skip] (15 characters at most)')
                if self.file_map < 0:
                    self.fail('@script before @map (this file is not a map\'s: say where its scripts start)')
                if not all(c.isdigit() or 'a' <= c <= 'z' or c == '_' for c in name):
                    self.fail('bad script name', name)
                if any(s['name'] == name for s in self.scripts):
                    self.fail('duplicate script', name)
                if len(self.scripts) >= MAX_SCRIPTS:
                    self.fail('too many scripts (32)')
                self.scripts.append(dict(name=name, first=len(self.ops), end=None, line=self.line, map=self.file_map, skip=bool(opt),
                                         file=self.fname))
                self.cur_map = self.file_map
                return
            if not self.in_script():
                self.fail('statement outside a @script', w)
            if w in ('@if', '@elif'):
                if w == '@elif':
                    if not self.ifs:
                        self.fail('@elif without @if')
                    f = self.ifs[-1]
                    if f['else']:
                        self.fail('@elif after @else')
                    if len(f['ends']) >= 12:
                        self.fail('too many @elif branches')
                    f['ends'].append(self.emit(5))
                    self.ops[f['jz']]['b'] = len(self.ops)
                else:
                    if len(self.ifs) >= 8:
                        self.fail('@if nested too deep (8)')
                    self.ifs.append(dict(ends=[], jz=-1, **{'else': False}, line=self.line))
                off, ln = self.compile_cond(rest)
                jz = self.emit(6, ln, 0, off)
                self.ifs[-1]['jz'] = jz
                return
            if w == '@else':
                if rest:
                    self.fail('@else takes nothing')
                if not self.ifs:
                    self.fail('@else without @if')
                f = self.ifs[-1]
                if f['else']:
                    self.fail('two @else')
                if len(f['ends']) >= 12:
                    self.fail('too many @elif branches')
                f['ends'].append(self.emit(5))
                self.ops[f['jz']]['b'] = len(self.ops)
                f['jz'] = -1
                f['else'] = True
                return
            if w == '@end':
                if rest:
                    self.fail('@end takes nothing')
                if not self.ifs:
                    self.fail('@end without @if')
                f = self.ifs.pop()
                if f['jz'] >= 0:
                    self.ops[f['jz']]['b'] = len(self.ops)
                for e in f['ends']:
                    self.ops[e]['b'] = len(self.ops)
                return
            self.fail('unknown directive', w)
        if not self.in_script():
            self.fail('statement outside a @script')
        if line[0] == ':':
            name = line[1:]
            if not name or len(name) > 15:
                self.fail(':label (15 characters at most)')
            if not all(c.isdigit() or 'a' <= c <= 'z' or c == '_' for c in name):
                self.fail('bad label', name)
            if name in self.labels:
                self.fail('duplicate label (labels are global)', name)
            if len(self.labels) >= MAX_LABELS:
                self.fail('too many labels (128)')
            self.labels[name] = (len(self.ops), self.line)
            return
        p = 0
        while p < len(line) and (line[p].isdigit() or 'a' <= line[p] <= 'z' or line[p] == '_'):
            p += 1
        if p > 0 and p < len(line) and line[p] == '(':
            while p < len(line) and line[p] != ')':
                p += 1
            if p < len(line):
                p += 1
        if p > 0 and p < len(line) and line[p] == ':' and (p + 1 == len(line) or line[p + 1] == ' '):
            self.stmt_page(line, p)
            return
        self.stmt_command(line)

    def compile(self, files):
        """files: [(file name, source, map index or -1)] in script.cpp's order (maps.txt)."""
        self.used_portraits = set()
        self.choices = []
        split = []
        for fname, src, fmap in files:
            self.fname = self.file = fname
            lines = split_lines(src)
            if len(lines) > 2048:
                self.line = 2049
                self.fail('more than 2048 lines')
            split.append((fname, lines, fmap))
        # pass 1: every file's @flags
        for fname, lines, fmap in split:
            self.fname = self.file = fname
            seen = False
            for n, ln in enumerate(lines, 1):
                s = ln.lstrip(' \t')
                if not s.startswith('@flags') or (len(s) > 6 and s[6] not in ' \t'):
                    continue
                self.line = n
                if seen:
                    self.fail('@flags declared twice')
                seen = True
                for tok in s[6:].split():
                    if len(tok) > 15:
                        self.fail('flag names are 15 characters at most', tok)
                    if not all(c.isdigit() or 'a' <= c <= 'z' or c == '_' for c in tok) or tok[0].isdigit():
                        self.fail('bad flag name', tok)
                    if tok in ('stage', 'potions', 'max_hp'):
                        self.fail('reserved name', tok)
                    if tok in self.flags:
                        self.fail('duplicate flag', tok)
                    if len(self.flags) >= MAX_FLAGS:
                        self.fail('more than 32 flags')
                    self.flags.append(tok)
        # pass 2, file by file
        for fname, lines, fmap in split:
            self.fname = self.file = fname
            self.file_map = self.cur_map = fmap
            i = 0
            while i < len(lines):
                self.line = i + 1
                logical = lines[i].lstrip(' \t')
                i += 1
                while i < len(lines) and lines[i].lstrip(' \t').startswith('|'):
                    logical += ' ' + lines[i].lstrip(' \t')
                    i += 1
                logical = logical[:511]
                if not logical:
                    continue
                if logical[0] == '|':
                    self.fail("a '|' continuation line must follow a choice")
                self.stmt(logical)
            self.line = len(lines)
            self.close_script()
        for op, name, call, ln, fname in self.fix:
            self.line, self.file = ln, fname
            if call:
                k = next((j for j, s in enumerate(self.scripts) if s['name'] == name), -1)
                if k < 0:
                    self.fail('call: unknown script', name)
                self.ops[op]['a'] = k
            else:
                if name not in self.labels:
                    self.fail('unknown label', name)
                self.ops[op]['b'] = self.labels[name][0]
        for s in self.scripts:
            self.file = s['file']
            for pc in range(s['first'], s['end']):
                o = self.ops[pc]
                if o['code'] in (5, 6, 4) and not (s['first'] <= o['b'] <= s['end']):
                    self.line = o['line']
                    self.fail('a label jumps into another script', s['name'])
        # map links: every map's entities, triggers, exits, glows (script.cpp link_maps)
        for mi, mname in enumerate(self.maps):
            sc = self.scans[mi]
            self.file = 'maps/%s.txt' % mname
            self.line = 0
            for e in sc['ents']:
                who = 'entity %s' % e['id']
                if e['talk'] and not any(s['name'] == e['talk'] for s in self.scripts):
                    self.fail('talk: unknown script', who)
                if e['win']:
                    if e['win'] not in self.flags:
                        self.fail('win: flag not declared in @flags', who)
                    self.flag_sets.add(e['win'])
                if e['cond']:
                    try:
                        self.compile_cond(e['cond'])
                    except LintError as ex:
                        raise LintError('%s (%s)' % (ex, who))
            for k, tr in enumerate(sc['trig']):
                self.line = tr['line']
                if not any(s['name'] == tr['script'] for s in self.scripts):
                    self.fail('trigger: unknown script', tr['script'])
                if tr['cond']:
                    self.compile_cond(tr['cond'])
            for x in sc['exits'] + sc['glows']:
                self.line = x['line']
                if x['cond']:
                    self.compile_cond(x['cond'])
        self.file = self.fname

    # -- the dump (== script_dump in src/script.cpp)
    def dump(self):
        L = ['flags %d%s' % (len(self.flags), ''.join(' ' + f for f in self.flags))]
        for i, s in enumerate(self.speakers):
            L.append('speaker %d %s "%s" %06X %s' % (i, s['id'], s['name'], s['rgb'], s['portrait']))
        for i in range(1, len(self.objectives)):
            L.append('objective %d %s' % (i, quoted(self.str_at(self.objectives[i]))))
        for m in self.markers:
            L.append('marker %s %s %s %s' % (self.maps[m['map']], self.scans[m['map']]['ents'][m['entity']]['id'], '!' if m['kind'] == 0 else '...',
                                             self.cond[m['cond']:m['cond'] + m['len']].hex()))
        L.append('actors %d%s' % (len(self.actors), ''.join(' ' + a for a in self.actors)))
        for si, s in enumerate(self.scripts):
            L.append('script %d %s %d %d %s%s' % (si, s['name'], s['first'], s['end'], self.maps[s['map']], ' skip' if s['skip'] else ''))
            for pc in range(s['first'], s['end']):
                o = self.ops[pc]
                c, ln = o['code'], '%d %s' % (pc, SC[o['code']])
                if c == 1:
                    ln += ' %s %s %d %s' % (self.speakers[o['a']]['id'], VARIANTS[o['b']], o['c'], quoted(self.str_at(o['str'])))
                elif c == 2:
                    ln += ' %d %s' % (o['c'], quoted(self.str_at(o['str'])))
                elif c == 3:
                    ln += ' %d cancel=%s' % (o['a'], '-' if o['b'] == 0xFF else str(o['b']))
                elif c == 4:
                    ln += ' -> %d %s' % (o['b'], quoted(self.str_at(o['str'])))
                elif c == 5:
                    ln += ' %d' % o['b']
                elif c == 6:
                    ln += ' %d %s' % (o['b'], self.cond[o['c']:o['c'] + o['a']].hex())
                elif c == 7:
                    ln += ' %s' % self.scripts[o['a']]['name']
                elif c in (8, 9, 12, 13, 16, 15):
                    ln += ' %d' % o['a']
                elif c in (10, 11):
                    ln += ' %s' % self.flags[o['a']]
                elif c == 17:
                    ln += ' %s %s' % (self.actor_name(o['a']), self.actor_name(o['b'] & 0xFF) if o['b'] & 0x100 else DIRS[o['b'] & 3])
                elif c == 18:
                    ln += ' %s %s' % (self.actor_name(o['a']), self.clips[o['b']])
                elif c == 20:
                    ln += ' %s %s' % (self.actor_name(o['a']), EMOTES[o['b'] % 5])
                elif c in (19, 21, 22):
                    ln += ' %d' % o['b']
                elif c == 23:
                    ln += ' %s' % ('on' if o['a'] else 'off')
                elif c == S_MOVE:
                    i8 = lambda v: v - 256 if v > 127 else v
                    ln += ' %s %d %d%s%s' % (self.actor_name(o['a']), i8(o['b'] & 0xFF), i8(o['b'] >> 8),
                                             ' rel' if o['c'] & MOVE_REL else '', ' run' if o['c'] & MOVE_RUN else '')
                elif c == S_PLACE:
                    ln += ' %s %d %d %s' % (self.actor_name(o['a']), o['b'] & 0xFF, o['b'] >> 8, DIRS[o['c'] & 3])
                elif c == S_CAM:
                    ln += ' %s %d' % ('field' if o['a'] == CAM_FIELD else self.scans[o['c'] % MAX_MAPS]['shots'][o['a']], o['b'])
                elif c == S_PAR:
                    ln += ' %d' % o['a']
                elif c == S_BATTLE:
                    ln += ' %s%s' % (self.actor_name(o['a']), ' boss' if o['b'] else '')
                elif c == S_MAP:
                    ln += ' %s %d %d %s %s' % (self.maps[o['a']], o['b'] & 0xFF, o['b'] >> 8, DIRS[o['c'] & 3],
                                               self.light_name((o['c'] >> 4) - 1) if o['c'] >> 4 else '-')
                elif c == S_LIGHT:
                    ln += ' %s' % self.light_name(o['a'])
                elif c == S_LIGHT_LERP:
                    ln += ' %s %s %d' % (self.light_name(o['a']), self.light_name(o['c']), o['b'])
                elif c in (S_NAMECARD, S_CARD):
                    ln += ' %s %d' % (quoted(self.str_at(o['str'])), o['b'])
                elif c == S_TRANSITION:
                    ln += ' T%d' % o['a']
                L.append(ln)
        return '\n'.join(L) + '\n'


def quoted(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


def split_lines(src):
    """src/script.cpp split_lines: comments ('#' at the start or after a blank, outside quotes), CR, trailing blanks."""
    out = []
    for ln in src.split('\n'):
        q, cut = False, len(ln)
        for i, ch in enumerate(ln):
            if ch == '"':
                q = not q
            elif ch == '#' and not q and (i == 0 or ln[i - 1] in ' \t'):
                cut = i
                break
            elif ch == '\r':
                cut = i
                break
        out.append(ln[:cut].rstrip(' \t'))
    if out and out[-1] == '' and src.endswith('\n'):
        out.pop()
    return out


def tokens(s):
    """next_token(): whitespace-separated, a "quoted string" is one token (quotes kept)."""
    out, i = [], 0
    while True:
        while i < len(s) and s[i] in ' \t':
            i += 1
        if i >= len(s):
            return out
        if s[i] == '"':
            j = s.find('"', i + 1)
            j = len(s) - 1 if j < 0 else j
            out.append(s[i:j + 1])
            i = j + 1
        else:
            j = i
            while j < len(s) and s[j] not in ' \t':
                j += 1
            out.append(s[i:j])
            i = j


# ---------------------------------------------------------------------------- analyses
def successors(c, pc):
    o = c.ops[pc]
    code = o['code']
    if code == 0:
        return []
    if code == 5:
        return [o['b']]
    if code == 6:
        return [pc + 1, o['b']]
    if code == 3:
        return [c.ops[pc + 1 + k]['b'] for k in range(o['a'])]
    if code == 4:
        return []
    if code == S_PAR:
        return [pc + 1 + o['a']]
    if code == S_TITLE:                        # the session ends
        return []
    return [pc + 1]


BLOCKING = (1, 2, 3, 19, 21, 22, S_MOVE, S_CAM, S_PAR, S_BATTLE, S_MAP, S_NAMECARD, S_CARD, S_TRANSITION, S_CREDITS, S_RESULTS)


def stage_constraints(c, off, taken):
    """Interval (lo, hi) the stage must be in when the JZ at condition `off` goes `taken` (True = condition held).
    Only `stage <op> literal` terms of a top-level && chain (or a single comparison when negated) are used."""
    src = c.cond_src.get(off, ('', 0))[0]
    if '||' in src or '(' in src:
        return (0, 255)
    terms = [t.strip() for t in src.split('&&')]
    lo, hi = 0, 255
    import re
    parsed = []
    for t in terms:
        m = re.fullmatch(r'stage\s*(==|!=|<=|>=|<|>)\s*(\d+)', t)
        if m:
            parsed.append((m.group(1), int(m.group(2))))
    if taken:
        for op, v in parsed:
            if op == '==': lo, hi = max(lo, v), min(hi, v)
            elif op == '>=': lo = max(lo, v)
            elif op == '>': lo = max(lo, v + 1)
            elif op == '<=': hi = min(hi, v)
            elif op == '<': hi = min(hi, v - 1)
        return (lo, hi)
    if len(terms) == 1 and len(parsed) == 1:
        op, v = parsed[0]
        neg = {'==': None, '!=': '==', '>=': '<', '>': '<=', '<=': '>', '<': '>='}[op]
        if neg is None:
            return (0, 255)
        return stage_constraints_single(neg, v)
    return (0, 255)


def stage_constraints_single(op, v):
    return {'==': (v, v), '>=': (v, 255), '>': (v + 1, 255), '<=': (0, v), '<': (0, v - 1)}[op]


def analyse(c, errors):
    entry_stage = (0, 7)
    for s in c.scripts:
        first, end = s['first'], s['end']
        # reachability (+ fall off the end)
        seen, work = set(), [first]
        while work:
            pc = work.pop()
            if pc >= end:
                errors.append('%s:%d: a path runs off the end of the script %s (missing \'end\')' % (s['file'], s['line'], s['name']))
                continue
            if pc in seen:
                continue
            seen.add(pc)
            work.extend(successors(c, pc))
        for pc in range(first, end):
            if pc not in seen and c.ops[pc]['code'] in (1, 2, 3):
                errors.append('%s:%d: unreachable %s in %s' % (c.ops[pc]['file'], c.ops[pc]['line'],
                                                              {1: 'page', 2: 'sys page', 3: 'choice'}[c.ops[pc]['code']], s['name']))
        # the branch walk: from every reachable op an END must be reachable, and no cycle may be free of blocking ops
        can_end = set()
        changed = True
        while changed:
            changed = False
            for pc in seen:
                if pc in can_end:
                    continue
                if c.ops[pc]['code'] in (0, S_TITLE) or any(n in can_end for n in successors(c, pc) if n < end):
                    can_end.add(pc); changed = True
        for pc in sorted(seen):
            if pc not in can_end:
                errors.append('%s:%d: dead end in %s: no path from here reaches an end (a loop with no exit)' % (c.ops[pc]['file'], c.ops[pc]['line'], s['name']))
                break
        # a cycle made only of non-blocking ops would spin forever within one tick
        nb = {pc for pc in seen if c.ops[pc]['code'] not in BLOCKING}
        color = {}
        def dfs(u):
            color[u] = 1
            for v in successors(c, u):
                if v in nb:
                    if color.get(v) == 1:
                        return True
                    if v not in color and dfs(v):
                        return True
            color[u] = 2
            return False
        for u in sorted(nb):
            if u not in color and dfs(u):
                errors.append('%s:%d: a loop in %s never blocks (no page, choice, wait or fade): the VM would stop it' % (c.ops[u]['file'], c.ops[u]['line'], s['name']))
                break
        # stage intervals (only moves forward)
        iv = {first: entry_stage}
        work = [first]
        steps = 0
        while work and steps < 20000:
            steps += 1
            pc = work.pop()
            if pc >= end:
                continue
            lo, hi = iv[pc]
            o = c.ops[pc]
            outs = []
            if o['code'] == 8:
                if hi > o['a']:
                    errors.append('%s:%d: set stage %d where the stage may already be %d (the stage only moves forward)' % (o['file'], o['line'], o['a'], hi))
                outs.append((pc + 1, (max(lo, o['a']), max(hi, o['a']))))
            elif o['code'] == 6:
                a = stage_constraints(c, o['c'], True)
                b = stage_constraints(c, o['c'], False)
                outs.append((pc + 1, (max(lo, a[0]), min(hi, a[1]))))
                outs.append((o['b'], (max(lo, b[0]), min(hi, b[1]))))
            else:
                outs = [(n, (lo, hi)) for n in successors(c, pc)]
            for n, (l2, h2) in outs:
                if l2 > h2:
                    continue
                if n in iv:
                    ol, oh = iv[n]
                    nl, nh = min(ol, l2), max(oh, h2)
                    if (nl, nh) == (ol, oh):
                        continue
                    iv[n] = (nl, nh)
                else:
                    iv[n] = (l2, h2)
                work.append(n)
    # cancel ("no") branches say how to come back: a page before every end
    for ch in c.choices:
        o = c.ops[ch['pc']]
        if o['b'] == 0xFF:
            continue
        start = c.ops[ch['pc'] + 1 + o['b']]['b']
        bad = set()
        def walk(pc, seen=()):
            if pc in seen or pc >= len(c.ops):
                return True
            code = c.ops[pc]['code']
            if code in (1, 2):
                return True
            if code == 0:
                return False
            if code == 3:
                return True
            return all(walk(n, seen + (pc,)) for n in successors(c, pc))
        if not walk(start):
            errors.append('%s:%d: the cancel option\'s branch ends without a page: it must tell the player how to come back (GDD 7.3)' % (c.ops[ch['pc']]['file'], ch['line']))
    # every flag read is set somewhere
    for f, ln in sorted(c.flag_reads.items(), key=lambda kv: kv[1]):
        if f not in c.flag_sets:
            errors.append('%s:%d: flag %s is read but never set (flag, give skill/token, or a map entity\'s win)' % (c.flag_read_file.get(f, c.fname), ln, f))


def check_portraits(c, romdisk, errors):
    for s in c.speakers:
        for v in VARIANTS:
            p = os.path.join(romdisk, 'portraits', '%s_%s.dtx' % (s['portrait'], v))
            if not os.path.exists(p):
                errors.append('speakers.txt:%d: speaker %s: portrait %s missing' % (s['line'], s['id'], os.path.relpath(p, romdisk)))
                continue
            b = open(p, 'rb').read(32)
            w, h, fmt = struct.unpack_from('<3H', b, 4)
            if b[:4] != b'DTX1' or (w, h, fmt) != (128, 128, 1) or os.path.getsize(p) != 32 + 128 * 128 * 2:
                errors.append('speakers.txt:%d: %s is not a 128x128 RGB565 .dtx' % (s['line'], os.path.relpath(p, romdisk)))


def read_clips(path):
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'battle3d'))
    import dmfio
    return [cl['name'] for cl in dmfio.read_dma(path)['clips']]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('romdisk', nargs='?', default=os.path.join(ROOT, 'romdisk'))
    ap.add_argument('--map', default='village', help='the map the game boots on (maps.txt decides the rest)')
    ap.add_argument('--script', help='lint this file in place of <romdisk>/scripts/<--as>.txt')
    ap.add_argument('--as', dest='as_name', default='village', help='the script file --script replaces (default village)')
    ap.add_argument('--dump', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    font = Font(os.path.join(a.romdisk, 'fonts', 'dialog.fnt'))
    clips = read_clips(os.path.join(a.romdisk, 'anims', 'rig_medium.dma'))
    errors = []
    try:
        maps, extra = read_maps_txt(a.romdisk, a.map)
        lights = read_lights(os.path.join(a.romdisk, 'maps', 'lights.txt'))
        scans = [scan_map(os.path.join(a.romdisk, 'maps', m + '.txt'), 'maps/%s.txt' % m, maps, lights) for m in maps]
        files = []
        for k, name in enumerate(maps + extra):
            path = os.path.join(a.romdisk, 'scripts', name + '.txt')
            fname = name + '.txt'
            if a.script and name == a.as_name:
                path, fname = a.script, os.path.basename(a.script)
            if not os.path.exists(path):
                continue
            src = open(path, encoding='latin-1').read()
            if len(src.encode('latin-1')) >= 16 * 1024:
                raise LintError('%s:0: script file missing or larger than 16 KiB' % fname)
            files.append((fname, src, k if k < len(maps) else -1))
        if not files:
            print('no script files')
            return 0
        c = Compiler(font, clips, os.path.join(a.romdisk, 'scripts', 'speakers.txt'), maps, scans, lights)
        c.compile(files)
        if extra and not os.path.exists(os.path.join(a.romdisk, 'scripts', 'credits.txt')):
            raise LintError('credits.txt:0: missing: the ending\'s `credits` rolls romdisk/scripts/credits.txt')
    except LintError as e:
        print(e)
        return 1
    if a.dump:
        sys.stdout.write(c.dump())
        return 0
    analyse(c, errors)
    check_portraits(c, a.romdisk, errors)
    for e in errors:
        print(e)
    if not a.quiet:
        n_pages = sum(1 for o in c.ops if o['code'] in (1, 2))
        print('%s: %d scripts, %d ops, %d pages, %d choices, %d flags, %d speakers, %d actors, %d maps: %s' % (
            ', '.join(f[0] for f in files), len(c.scripts), len(c.ops), n_pages, len(c.choices), len(c.flags), len(c.speakers),
            len(c.actors), len(maps), 'clean' if not errors else '%d errors' % len(errors)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
