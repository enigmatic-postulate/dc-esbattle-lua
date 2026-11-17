#!/usr/bin/env python3
"""Prototype for the dialog slice plan: can ~95 ASCII glyphs of a proportional font at a 16 px line
be packed into a 256x128 atlas, and do the GDD Appendix A pages fit 3 lines x 420 px?
Usage: font_fit.py <font.ttf> [px ...]   (stand-in font for measuring only; the shipped font must be CC0, GDD A2)"""
import sys
from PIL import ImageFont

PAGES = [  # in-scope pages of GDD Appendix A (wren_main, brom_main) plus the slice's own lines; '|' = forced break
 "You fought them already? Both patrols? Then you know.",
 "Oh! A knight. A real one. Thank the stars.",
 "The dead are walking again.|Two patrols came over the hill last night,",
 "and the old barrow gate is humming.",
 "Skeletons left: the counter says it all. Fire burns them, and burning keeps going.",
 "Both patrols? You're quicker than my kettle.",
 "Take this token - Corwin will let you pass. And hold still...",
 "Wren's blessing! Max HP +10, fully healed.",
 "Corwin is waiting at the east road. Be careful in the barrow.",
 "Long ago a guardian was bound under the barrow.|The Bone Warden. He keeps the dead asleep.",
 "Something cracked his seal.",
 "Then take this. Burn the bones, not the barns.",
 "Rowan learned FIREBALL.",
 "Come back before they reach the houses. Please.",
 "Ho! A knight, and a rusty one. Here - three potions. Mind the cork, it bites.",
 "...Thank you.",
 "Don't thank me. Come back alive and buy a new helmet.",
 "Need a rest? My fire's warm and my stew is only slightly alarming.",
 "Good as new. Well. Good as used.",
 "Potions refilled.",
]
CHOICES = ["I will deal with them.", "Tell me more.", "Not now.", "Rest (full heal)", "Later."]

def wrap(font, text, width):
    lines = []
    for para in text.split('|'):
        cur = ""
        for w in para.split(' '):
            t = (cur + " " + w) if cur else w
            if font.getlength(t) <= width: cur = t
            else: lines.append(cur); cur = w
        lines.append(cur)
    return lines

def pack(font, cell_h, aw=256, ah=128):
    x = y = 0
    for c in range(32, 127):
        w = int(round(font.getlength(chr(c)))) + 1   # 1 px gutter
        if x + w > aw: x = 0; y += cell_h
        x += w
    return y + cell_h <= ah, y + cell_h

font_path = sys.argv[1]
sizes = [int(s) for s in sys.argv[2:]] or [13, 14, 15, 16]
for px in sizes:
    f = ImageFont.truetype(font_path, px)
    asc, desc = f.getmetrics()
    fits, used = pack(f, 16)
    worst = max(len(wrap(f, p, 420)) for p in PAGES)
    avg = sum(f.getlength(chr(c)) for c in range(97, 123)) / 26
    print(f"size {px}: ascent+descent {asc+desc} px, atlas 256x128 {'fits' if fits else 'OVERFLOWS'} ({used} rows px), "
          f"avg lower-case advance {avg:.1f} px (~{int(420/avg)} chars/line), worst page {worst} lines, "
          f"widest choice {max(f.getlength(c) for c in CHOICES):.0f} px")
