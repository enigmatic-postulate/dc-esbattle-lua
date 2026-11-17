#!/usr/bin/env python3
"""
Field invariant checker (DoD F3, F4, F8, F9) over a host trace (full or --state-only).
Reads the map file independently of the C++ loader and asserts, for every tick:
  F3  the hero's feet (the 16x12 feet box's centre) on an in-bounds, walkable tile; the feet box never overlaps a
      present entity's 24x20 body box (contact starts a battle instead; NPCs block)
  F4  camera clamped to the map; hero (32x32 cell) fully on screen (the legacy 2D window, still traced)
  F8  after a won battle: back in the field on the tile where contact happened, foe gone, HP kept
  F9  after Game Over -> A: hero on the checkpoint tile (the spawn on a map without a quest), full HP, every entity
      present (the test map has no checkpoints after defeats)
  boot: first tick - hero at spawn, every entity alive at home
  --expect-wins N / --expect-restarts N: the scenario must produce exactly that many
  (guards against vacuous passes, e.g. a bug that prevents battles from ever starting)
usage: check_invariants.py <trace> [map.txt] [--expect-wins N] [--expect-restarts N]

Slice 3D-5 (task 0.2): updated to the trace format since the analog field (hero=<x>,<y> in 1/16 px, the sprite
cell's top-left; entities <id>:<tile x>,<tile y>:<px>,<py>:<alive|gone|hidden>). The previous version expected the
tile-stepping hero of the 2D build and silently matched no line at all (0 field ticks checked).
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
SUB, TILE = 16, 32

pat = re.compile(r"^X mode=(\w+) fade=(\d+) hero=(-?\d+),(-?\d+),(\w+),(mv|idle) cam=(-?\d+),(-?\d+) hp=(\d+)/(\d+) pending=(-?\d+) ents=(.*)$")
errors, ticks, field_ticks, battles_won, restarts = [], 0, 0, 0, 0
contact_tile = None; hp_before = None; prev_mode = None
for n, line in enumerate(open(trace), 1):
    if not line.startswith("X "): continue
    ticks += 1
    m = pat.match(line.strip())
    if not m:          # battle snapshot line
        prev_mode = "battle"; continue
    mode, fade, hx, hy, hdir, mv, cx, cy, hp, hpmax, pending, ents = m.groups()
    hx, hy, cx, cy, hp, hpmax, pending = map(int, (hx, hy, cx, cy, hp, hpmax, pending))
    fx, fy = hx + 16 * SUB, hy + 24 * SUB                 # the feet box's centre, 1/16 px
    tx, ty = fx // (TILE * SUB), fy // (TILE * SUB)
    ent, boxes = {}, {}
    for e in re.findall(r"(\w+):(\d+),(\d+):(-?\d+),(-?\d+):(alive|gone|hidden)", ents):
        ent[e[0]] = (int(e[1]), int(e[2]), e[5] == "alive")
        epx, epy = int(e[3]), int(e[4])
        boxes[e[0]] = ((epx + 4) * SUB, (epy + 10) * SUB, (epx + 28) * SUB, (epy + 30) * SUB)   # ENT_BOX (field.h)
    def err(msg): errors.append(f"trace line {n}: {msg}")
    if prev_mode is None:                                # boot
        if (hx, hy) != (spawn[0] * TILE * SUB, spawn[1] * TILE * SUB): err(f"boot: hero at {hx},{hy}, spawn is {spawn}")
        for eid, (ex, ey, alive) in ent.items():
            if not alive or (ex, ey) != homes[eid]: err(f"boot: {eid} not alive at home ({ex},{ey},{alive})")
        if set(ent) != set(homes): err(f"boot: entities {sorted(ent)} != map {sorted(homes)}")
    if mode in ("field", "to_battle"):
        field_ticks += 1
        if not (0 <= tx < W and 0 <= ty < H): err(f"hero out of bounds {tx},{ty}")
        elif rows[ty][tx] not in WALK: err(f"hero on blocking tile {rows[ty][tx]!r} at {tx},{ty}")
        hb = (hx + 8 * SUB, hy + 18 * SUB, hx + 24 * SUB, hy + 30 * SUB)                    # HERO_BOX (field.h)
        for eid, (ex, ey, alive) in ent.items():
            b = boxes[eid]
            if alive and mode == "field" and hb[0] < b[2] and b[0] < hb[2] and hb[1] < b[3] and b[1] < hb[3]:
                err(f"hero's feet box overlaps {eid}'s body at {tx},{ty}")
            if alive and rows[ey][ex] not in WALK: err(f"{eid} on blocking tile at {ex},{ey}")
        if not (0 <= cx <= W * 32 - 640 and 0 <= cy <= H * 32 - 480): err(f"camera out of range {cx},{cy}")
        px, py = hx // SUB - cx, hy // SUB - cy
        if not (-32 <= px <= 640 and -32 <= py <= 480): err(f"hero off screen at {px},{py}")
    if mode == "to_battle" and prev_mode == "field":
        contact_tile, hp_before = (tx, ty), hp
    if prev_mode == "to_field" and mode == "field":      # returned after a win
        battles_won += 1
        if contact_tile and (tx, ty) != contact_tile: err(f"F8: returned to {tx},{ty}, contact was at {contact_tile}")
        gone = [k for k, v in ent.items() if not v[2]]
        if not gone: err("F8: no entity removed after a win")
    if prev_mode == "game_over" and mode == "field":     # retry after Game Over
        restarts += 1
        if (tx, ty) != spawn: err(f"F9: restart at {tx},{ty}, spawn is {spawn}")
        if hp != hpmax: err(f"F9: restart with {hp}/{hpmax} HP")
        for eid, (ex, ey, alive) in ent.items():
            if not alive or (ex, ey) != homes[eid]: err(f"F9: {eid} not restored ({ex},{ey},{alive})")
    prev_mode = mode

if expect_wins is not None and battles_won != expect_wins: errors.append(f"expected {expect_wins} won battle(s), got {battles_won}")
if expect_restarts is not None and restarts != expect_restarts: errors.append(f"expected {expect_restarts} restart(s), got {restarts}")
if field_ticks == 0: errors.append("no field tick matched the trace format (vacuous)")
print(f"{trace}: {ticks} ticks ({field_ticks} field), battles won+returned {battles_won}, game-over restarts {restarts}, "
      f"{'OK' if not errors else str(len(errors)) + ' VIOLATIONS'}")
for e in errors[:10]: print("  " + e)
sys.exit(1 if errors else 0)
