#!/usr/bin/env python3
"""DoD F7: A enters targeting, B cancels back to the command ring, Y jumps to Guard.
Reads a FULL trace of tests/inputs/battle_ui_buttons.in (battle-test mode) and inspects the
icon-atlas draws: sword = cell (0,0), shield = cell (0.5,0), target arrow = cell (0.75,0)."""
import sys
frames = {}; cur = None
for l in open(sys.argv[1]):
    if l.startswith("F "): cur = l.split()[1]; frames[cur] = []; continue
    if cur: frames[cur].append(l.split())
def icons(f):
    return {(float(t[8]), float(t[9])): float(t[4]) for t in frames[f] if t[0] == "S" and t[1].endswith("ui_icons.png")}
checks = [
    ("20", "A enters targeting", lambda r: (0.0, 0.0) not in r and (0.75, 0.0) in r),
    ("45", "B cancels to ring with Attack selected", lambda r: r.get((0.0, 0.0)) == 48 and r.get((0.5, 0.0)) == 32),
    ("60", "Y selects Guard", lambda r: r.get((0.0, 0.0)) == 32 and r.get((0.5, 0.0)) == 48),
]
bad = [f"tick {f}: {name}" for f, name, ok in checks if not ok(icons(f))]
print("F7 buttons: " + ("OK" if not bad else "FAILED " + "; ".join(bad)))
sys.exit(1 if bad else 0)
