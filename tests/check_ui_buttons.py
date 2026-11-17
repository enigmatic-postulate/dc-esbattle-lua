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
    # slice 3D-5 triage: since the 3D-4 UI the chosen command stays visible above the hero while targeting (the
    # sword at 32 px, not the ring's 48 px selected icon); the rest of the ring is gone and the target arrow shows
    ("20", "A enters targeting", lambda r: (0.75, 0.0) in r and r.get((0.0, 0.0)) == 32 and (0.25, 0.0) not in r),
    ("45", "B cancels to ring with Attack selected", lambda r: r.get((0.0, 0.0)) == 48 and r.get((0.5, 0.0)) == 32),
    ("60", "Y selects Guard", lambda r: r.get((0.0, 0.0)) == 32 and r.get((0.5, 0.0)) == 48),
]
bad = [f"tick {f}: {name}" for f, name, ok in checks if not ok(icons(f))]
print("F7 buttons: " + ("OK" if not bad else "FAILED " + "; ".join(bad)))
sys.exit(1 if bad else 0)
