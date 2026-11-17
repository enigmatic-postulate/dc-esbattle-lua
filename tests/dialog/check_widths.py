#!/usr/bin/env python3
"""check_widths.py - text metrics parity: src/text.cpp text_width / text_glyph_count == tools/dialog/lint.py's,
for every page line and choice of the shipped scripts plus every printable character and the colour codes.
    python3 tests/dialog/check_widths.py BIN [romdisk]"""
import os, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'dialog'))
import lint
binp = sys.argv[1]
rd = sys.argv[2] if len(sys.argv) > 2 else 'romdisk'
font = lint.Font(os.path.join(rd, 'fonts', 'dialog.fnt'))
texts = []
for ln in open(os.path.join(rd, 'scripts', 'village.txt'), encoding='latin-1'):
    ln = lint.split_lines(ln)[0] if lint.split_lines(ln) else ''
    s = ln.strip()
    if ':' in s and not s.startswith((':', '@', 'choice', 'objective')):
        body = s.split(':', 1)[1].strip()
        texts += body.split('|')
    elif s.startswith('choice') or s.startswith('|'):
        texts += [p.split('"')[1] for p in s.split('|') if '"' in p]
    elif s.startswith('objective'):
        texts.append(s.split('"')[1])
texts += [chr(c) for c in range(32, 127)] + ['{gold}Gold{/} and {red}red{/} and {grey}grey{/}', 'A,B.C!D?E...F', '']
texts = [t for t in texts if '\n' not in t]
with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False, encoding='latin-1') as f:
    f.write('\n'.join(texts) + '\n')
    path = f.name
out = subprocess.run([binp, '-', '/dev/null', '--text-widths', path, '--romdisk', rd], capture_output=True, text=True).stdout.split('\n')
os.unlink(path)
bad = 0
for t, o in zip(texts, [x for x in out if x.startswith('W ')]):
    w, g = map(int, o.split()[1:3])
    if (w, g) != (font.width(t), font.glyph_count(t)):
        bad += 1
        if bad < 5: print('  %r: C++ %d px %d glyphs, Python %d px %d glyphs' % (t, w, g, font.width(t), font.glyph_count(t)))
n = len([x for x in out if x.startswith('W ')])
if bad or n != len(texts):
    print('text widths: %d of %d differ (%d answered)' % (bad, len(texts), n)); sys.exit(1)
print('text widths: C++ == Python for %d strings (every page line, choice and objective, the 95 characters, colour codes)' % n)
