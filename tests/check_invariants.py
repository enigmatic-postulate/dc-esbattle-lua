#!/usr/bin/env python3
"""
Field invariant checker (DoD F3, F4, F8, F9) over a host trace (full or --state-only).
Reads the map file independently of the C++ loader and asserts, for every tick:
  F3  hero tile in bounds and walkable; never on an alive entity's tile
  F4  camera clamped to the map; hero (32x32) fully on screen
  F8  after a won battle: back in the field on the tile where contact happened, foe gone, HP kept
  F9  after Game Over -> A: hero at spawn, full HP, every entity alive
  boot: first tick - hero at spawn, every entity alive at home
  --expect-wins N / --expect-restarts N: the scenario must produce exactly that many
  (guards against vacuous passes, e.g. a bug that prevents battles from ever starting)
usage: check_invariants.py <trace> [map.txt] [--expect-wins N] [--expect-restarts N]
"""
import re, sys

args = sys.argv[1:]
def opt(name):
    if name in args:
        i = args.index(name); v = int(args[i + 1]); del args[i:i + 2]; return v
    return None
expect_wins, expect_restarts = opt("--expect-wins"), opt("--expect-restarts")
trace = args[0]
mpath = args[1] if len(args) > 1 else "romdisk/maps/test_field.txt"
lines = open(mpath).read().splitlines()
i = next(k for k, l in enumerate(lines) if l.startswith("map "))
W, H = map(int, lines[i].split()[1:3]); rows = lines[i + 1:i + 1 + H]
spawn = next(tuple(map(int, l.split()[1:3])) for l in lines if l.startswith("spawn "))
homes = {l.split()[1]: (int(l.split()[4]), int(l.split()[5])) for l in lines if l.startswith("entity ")}
WALK = set(".,=")

pat = re.compile(r"^X mode=(\w+) fade=(\d+) hero=(\d+),(\d+),(\w+),(mv|idle)(\d+) cam=(-?\d+),(-?\d+) hp=(\d+)/(\d+) pending=(-?\d+) ents=(.*)$")
errors, ticks, field_ticks, battles_won, restarts = [], 0, 0, 0, 0
prev = None; contact_tile = None; hp_before = None; pending_id = None; prev_mode = None
for n, line in enumerate(open(trace), 1):
    if not line.startswith("X "): continue
    ticks += 1
    m = pat.match(line.strip())
    if not m:          # battle snapshot line
        prev_mode = "battle"; continue
    mode, fade, hx, hy, hdir, mv, step, cx, cy, hp, hpmax, pending, ents = m.groups()
    hx, hy, step, cx, cy, hp, hpmax, pending = map(int, (hx, hy, step, cx, cy, hp, hpmax, pending))
    ent = {}
    for e in re.findall(r"(\w+):(\d+),(\d+):(alive|gone)", ents):
        ent[e[0]] = (int(e[1]), int(e[2]), e[3] == "alive")
    def err(msg): errors.append(f"trace line {n}: {msg}")
    if prev_mode is None:                                # boot
        if (hx, hy) != spawn: err(f"boot: hero at {hx},{hy}, spawn is {spawn}")
        for eid, (ex, ey, alive) in ent.items():
            if not alive or (ex, ey) != homes[eid]: err(f"boot: {eid} not alive at home ({ex},{ey},{alive})")
        if set(ent) != set(homes): err(f"boot: entities {sorted(ent)} != map {sorted(homes)}")
    if mode in ("field", "to_battle"):
        field_ticks += 1
        if not (0 <= hx < W and 0 <= hy < H): err(f"hero out of bounds {hx},{hy}")
        elif rows[hy][hx] not in WALK: err(f"hero on blocking tile {rows[hy][hx]!r} at {hx},{hy}")
        for eid, (ex, ey, alive) in ent.items():
            if alive and (ex, ey) == (hx, hy): err(f"hero overlaps {eid} at {hx},{hy}")
            if alive and rows[ey][ex] not in WALK: err(f"{eid} on blocking tile at {ex},{ey}")
        if not (0 <= cx <= W * 32 - 640 and 0 <= cy <= H * 32 - 480): err(f"camera out of range {cx},{cy}")
        px, py = hx * 32 - cx, hy * 32 - cy
        if not (-32 <= px <= 640 and -32 <= py <= 480): err(f"hero off screen at {px},{py}")
    if mode == "to_battle" and prev_mode == "field":
        contact_tile, hp_before, pending_id = (hx, hy), hp, pending
    if prev_mode == "to_field" and mode == "field":      # returned after a win
        battles_won += 1
        if contact_tile and (hx, hy) != contact_tile: err(f"F8: returned to {hx},{hy}, contact was at {contact_tile}")
        gone = [k for k, v in ent.items() if not v[2]]
        if not gone: err("F8: no entity removed after a win")
    if prev_mode == "game_over" and mode == "field":     # retry after Game Over
        restarts += 1
        if (hx, hy) != spawn: err(f"F9: restart at {hx},{hy}, spawn is {spawn}")
        if hp != hpmax: err(f"F9: restart with {hp}/{hpmax} HP")
        for eid, (ex, ey, alive) in ent.items():
            if not alive or (ex, ey) != homes[eid]: err(f"F9: {eid} not restored ({ex},{ey},{alive})")
    prev_mode = mode; prev = m

if expect_wins is not None and battles_won != expect_wins: errors.append(f"expected {expect_wins} won battle(s), got {battles_won}")
if expect_restarts is not None and restarts != expect_restarts: errors.append(f"expected {expect_restarts} restart(s), got {restarts}")
print(f"{trace}: {ticks} ticks ({field_ticks} field), battles won+returned {battles_won}, game-over restarts {restarts}, "
      f"{'OK' if not errors else str(len(errors)) + ' VIOLATIONS'}")
for e in errors[:10]: print("  " + e)
sys.exit(1 if errors else 0)
