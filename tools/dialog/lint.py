#!/usr/bin/env python3
"""lint.py - the script compiler in Python, and the GDD 7.4 validation (slice 3D-5, task C3).

    python3 tools/dialog/lint.py [romdisk] [--map village] [--script FILE] [--dump] [--quiet]

Compiles romdisk/scripts/<map>.txt + speakers.txt exactly as src/script.cpp does (the same pools, the same
wrapping with the real font metrics from romdisk/fonts/dialog.fnt, the same condition byte code), so that
--dump prints the same text as `battle_trace --dump-scripts` (tests/dialog: parity). Then checks:
  * every label, script and actor reference exists; a choice has 1-3 options; the cancel option is last
  * every flag that is read (conditions, @marker, the map's `if`) is set somewhere (flag, give skill/token,
    or a map entity's `win`)
  * no unreachable page (a page, sys page or choice no path from its script's start reaches)
  * every page fits the box: 3 lines of 420 px with the real font metrics; every choice fits its box
  * every speaker has a name, a colour and its three portraits (romdisk/portraits/<base>_<variant>.dtx)
  * the branch walk: every path ends (no run off the end, no loop with no exit, no loop that never blocks)
  * a cancel ("no") branch shows a page before it ends: it tells the player how to come back (GDD 7.3)
  * the stage only moves forward: `set stage N` where the stage may already be above N is an error
  * commands that are not in this slice (cam, move, battle, ...) are errors
Exit status 0 when clean. Errors are printed as file:line: message.
"""
import argparse
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

SC = ['END', 'PAGE', 'SYS', 'CHOICE', 'OPTION', 'JMP', 'JZ', 'CALL', 'STAGE', 'MAXHP', 'FLAG', 'UNFLAG', 'GIVE_POTIONS',
      'TAKE_POTIONS', 'HEAL', 'CHECKPOINT', 'OBJECTIVE', 'FACE', 'ANIM', 'WAIT', 'EMOTE', 'FADE_OUT', 'FADE_IN', 'LETTERBOX']
VARIANTS = ['neutral', 'happy', 'worried']
EMOTES = ['!', '?', '...', 'sweat', 'anger']
DIRS = ['down', 'up', 'left', 'right']
OUT_OF_SCOPE = ['cam', 'move', 'battle', 'map', 'light', 'namecard', 'card', 'credits', 'results', 'title', 'transition']
C_PUSH, C_STAGE, C_POTIONS, C_MAXHP, C_FLAG, C_NOT, C_ADD, C_SUB, C_EQ, C_NE, C_LT, C_LE, C_GT, C_GE, C_AND, C_OR = range(1, 17)
PAGE_WIDTH, PAGE_LINES, CHOICE_TEXT_MAX = 420, 3, 300
MAX_OPS, STR_BYTES, COND_BYTES, MAX_SCRIPTS, MAX_LABELS, MAX_FLAGS = 1024, 12 * 1024, 1024, 32, 128, 32
MAX_SPEAKERS, MAX_MARKERS, MAX_OBJECTIVES, COND_MAX = 8, 8, 32, 32
CODES = ['{gold}', '{red}', '{grey}', '{/}']


class LintError(Exception):
    pass


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


# ---------------------------------------------------------------------------- map entities
def read_map(path):
    ents = []
    for ln in open(path).read().split('\n'):
        if not ln.startswith('entity '):
            continue
        body = ln[7:]
        h = body.find(' #')
        if h >= 0:
            body = body[:h]
        cond = ''
        c = body.find(' if ')
        if c >= 0:
            cond = body[c + 4:].strip()
            body = body[:c]
        t = body.split()
        e = dict(id=t[0], kind=t[1], look=t[2], talk='', win='', cond=cond)
        k = 6
        if t[1] == 'npc' and len(t) > k + 1 and t[k] == 'talk':
            e['talk'] = t[k + 1]
        while k < len(t):
            if t[k] == 'win' and k + 1 < len(t):
                e['win'] = t[k + 1]
            k += 1
        ents.append(e)
    return ents


# ---------------------------------------------------------------------------- the compiler (script.cpp)
class Compiler:
    def __init__(self, font, ents, clips, fname, speakers_path):
        self.font, self.ents, self.clips, self.fname = font, ents, clips, fname
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
        self.file = fname
        self.cond_src = {}            # cond offset -> (source, line) for the analyses
        self.flag_reads = {}          # flag -> first line read
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
        self.ops.append(dict(code=code, a=a & 0xFF, b=b & 0xFFFF, c=c & 0xFFFF, str=s & 0xFFFF, line=self.line))
        return len(self.ops) - 1

    def flag_find(self, name):
        return self.flags.index(name) if name in self.flags else -1

    def actor_find(self, name):
        if name == 'rowan':
            return 0
        for i, e in enumerate(self.ents):
            if e['id'] == name:
                return 1 + i
        return -1

    def actor_name(self, a):
        return 'rowan' if a == 0 else (self.ents[a - 1]['id'] if a - 1 < len(self.ents) else '?')

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
            self.fix.append((op, labels[i][:15], False, self.line))

    def stmt_command(self, line):
        t = tokens(line)
        w = t[0]
        if w in OUT_OF_SCOPE:
            self.fail('command not in this slice', w)
        if w == 'choice':
            self.stmt_choice(line[len('choice'):].lstrip(' \t'))
            return
        if '|' in line:
            self.fail('parallel commands (cmd | cmd) are not in this slice')
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
            op = self.emit(5); self.fix.append((op, a1[:15], False, self.line)); return
        if w == 'call':
            if not a1 or a2:
                self.fail('call takes one script name')
            op = self.emit(7); self.fix.append((op, a1[:15], True, self.line)); return
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
            ids = {'c1': 1, 'c2': 2, 'rest': 9}
            if a1 and a1 not in ids:
                self.fail('checkpoint [c1|c2|rest]', a1)
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
            if w == '@marker':
                if self.in_script():
                    self.fail('@marker inside a script')
                eid = t[1] if len(t) > 1 else ''
                kind = t[2] if len(t) > 2 else ''
                a = self.actor_find(eid)
                if a <= 0 or self.ents[a - 1]['kind'] != 'npc':
                    self.fail('@marker: not an npc of the map', eid)
                if kind not in ('!', '...'):
                    self.fail('@marker kind must be ! or ...', kind)
                if len(self.markers) >= MAX_MARKERS:
                    self.fail('too many markers (8)')
                crest = rest[len(eid):].lstrip(' \t')[len(kind):]
                off, ln = self.compile_cond(crest)
                self.markers.append(dict(entity=a - 1, kind=0 if kind == '!' else 1, cond=off, len=ln))
                return
            if w == '@script':
                self.close_script()
                name = t[1] if len(t) > 1 else ''
                if not name or len(t) > 2 or len(name) > 15:
                    self.fail('@script <name> (15 characters at most)')
                if not all(c.isdigit() or 'a' <= c <= 'z' or c == '_' for c in name):
                    self.fail('bad script name', name)
                if any(s['name'] == name for s in self.scripts):
                    self.fail('duplicate script', name)
                if len(self.scripts) >= MAX_SCRIPTS:
                    self.fail('too many scripts (32)')
                self.scripts.append(dict(name=name, first=len(self.ops), end=None, line=self.line))
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

    def compile(self, src):
        self.used_portraits = set()
        self.choices = []
        lines = split_lines(src)
        if len(lines) > 2048:
            self.line = 2049
            self.fail('more than 2048 lines')
        # pass 1: @flags
        for n, ln in enumerate(lines, 1):
            s = ln.lstrip(' \t')
            if not s.startswith('@flags') or (len(s) > 6 and s[6] not in ' \t'):
                continue
            self.line = n
            if self.flags:
                self.fail('@flags declared twice')
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
        # pass 2
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
        for op, name, call, ln in self.fix:
            self.line = ln
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
            for pc in range(s['first'], s['end']):
                o = self.ops[pc]
                if o['code'] in (5, 6, 4) and not (s['first'] <= o['b'] <= s['end']):
                    self.line = o['line']
                    self.fail('a label jumps into another script', s['name'])
        # map links
        self.file = 'maps/%s' % os.path.basename(self.map_file)
        self.line = 0
        for e in self.ents:
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
        self.file = self.fname

    # -- the dump (== script_dump in src/script.cpp)
    def dump(self):
        L = ['flags %d%s' % (len(self.flags), ''.join(' ' + f for f in self.flags))]
        for i, s in enumerate(self.speakers):
            L.append('speaker %d %s "%s" %06X %s' % (i, s['id'], s['name'], s['rgb'], s['portrait']))
        for i in range(1, len(self.objectives)):
            L.append('objective %d %s' % (i, quoted(self.str_at(self.objectives[i]))))
        for m in self.markers:
            L.append('marker %s %s %s' % (self.actor_name(1 + m['entity']), '!' if m['kind'] == 0 else '...',
                                          self.cond[m['cond']:m['cond'] + m['len']].hex()))
        for si, s in enumerate(self.scripts):
            L.append('script %d %s %d %d' % (si, s['name'], s['first'], s['end']))
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
    return [pc + 1]


BLOCKING = (1, 2, 3, 19, 21, 22)


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
                errors.append('%s:%d: a path runs off the end of the script %s (missing \'end\')' % (c.fname, s['line'], s['name']))
                continue
            if pc in seen:
                continue
            seen.add(pc)
            work.extend(successors(c, pc))
        for pc in range(first, end):
            if pc not in seen and c.ops[pc]['code'] in (1, 2, 3):
                errors.append('%s:%d: unreachable %s in %s' % (c.fname, c.ops[pc]['line'],
                                                              {1: 'page', 2: 'sys page', 3: 'choice'}[c.ops[pc]['code']], s['name']))
        # the branch walk: from every reachable op an END must be reachable, and no cycle may be free of blocking ops
        can_end = set()
        changed = True
        while changed:
            changed = False
            for pc in seen:
                if pc in can_end:
                    continue
                if c.ops[pc]['code'] == 0 or any(n in can_end for n in successors(c, pc) if n < end):
                    can_end.add(pc); changed = True
        for pc in sorted(seen):
            if pc not in can_end:
                errors.append('%s:%d: dead end in %s: no path from here reaches an end (a loop with no exit)' % (c.fname, c.ops[pc]['line'], s['name']))
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
                errors.append('%s:%d: a loop in %s never blocks (no page, choice, wait or fade): the VM would stop it' % (c.fname, c.ops[u]['line'], s['name']))
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
                    errors.append('%s:%d: set stage %d where the stage may already be %d (the stage only moves forward)' % (c.fname, o['line'], o['a'], hi))
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
            errors.append('%s:%d: the cancel option\'s branch ends without a page: it must tell the player how to come back (GDD 7.3)' % (c.fname, ch['line']))
    # every flag read is set somewhere
    for f, ln in sorted(c.flag_reads.items(), key=lambda kv: kv[1]):
        if f not in c.flag_sets:
            errors.append('%s:%d: flag %s is read but never set (flag, give skill/token, or a map entity\'s win)' % (c.fname, ln, f))


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
    ap.add_argument('--map', default='village')
    ap.add_argument('--script', help='lint this file instead of <romdisk>/scripts/<map>.txt')
    ap.add_argument('--dump', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    font = Font(os.path.join(a.romdisk, 'fonts', 'dialog.fnt'))
    map_file = os.path.join(a.romdisk, 'maps', a.map + '.txt')
    ents = read_map(map_file)
    clips = read_clips(os.path.join(a.romdisk, 'anims', 'rig_medium.dma'))
    path = a.script or os.path.join(a.romdisk, 'scripts', a.map + '.txt')
    errors = []
    try:
        c = Compiler(font, ents, clips, a.map + '.txt' if not a.script else os.path.basename(path),
                     os.path.join(a.romdisk, 'scripts', 'speakers.txt'))
        c.map_file = map_file
        src = open(path, encoding='latin-1').read()
        if len(src.encode('latin-1')) >= 16 * 1024:
            raise LintError('%s:0: script file missing or larger than 16 KiB' % os.path.basename(path))
        c.compile(src)
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
        print('%s: %d scripts, %d ops, %d pages, %d choices, %d flags, %d speakers: %s' % (
            os.path.basename(path), len(c.scripts), len(c.ops), n_pages, len(c.choices), len(c.flags), len(c.speakers),
            'clean' if not errors else '%d errors' % len(errors)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
