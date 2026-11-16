#!/usr/bin/env python3
"""
Generates deterministic input scripts: one line per frame "<dt> <buttons-hex>".
dt is written as the exact float32 value main.cpp would compute
((now-last)/1000.0f, clamped to 0.1f), so both the Lua reference and the C++
port consume bit-identical dt values.

Usage: gen_inputs.py <scenario> <dtmode> <seed> <frames> <out>
  scenario: fireball_win | attack_win | guard_lose | cancel | hold_a | lock_edge | fuzz
            field_walk | field_win | field_lose | field_full | field_fuzz   (field map romdisk/maps/test_field.txt)
  dtmode:   ms60 | jitter | dyadic
"""
import sys, random, struct

UP, DOWN, LEFT, RIGHT, FIRE, B, Y, START = 1, 2, 4, 8, 16, 32, 64, 128

def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]

def dt_stream(mode, rng):
    pattern = [16, 17, 17]
    i = 0
    while True:
        if mode == "dyadic":
            yield 1.0 / 64.0
        elif mode == "ms60":
            yield f32(f32(pattern[i % 3]) / f32(1000.0)); i += 1
        elif mode == "jitter":
            ms = rng.choice([16, 16, 17, 17, 33, 50, 5, 1, 120])
            d = f32(f32(ms) / f32(1000.0))
            yield d if d <= f32(0.1) else f32(0.1)
        else:
            raise SystemExit("bad dtmode")

def press(btn, hold=3, gap=3):
    return [btn] * hold + [0] * gap

def wait(n):
    return [0] * n

def scenario(name, rng, frames):
    seq = []
    if name == "fireball_win":
        while len(seq) < frames:
            seq += press(RIGHT) + press(FIRE) + wait(70)
    elif name == "attack_win":
        k = 0
        while len(seq) < frames:
            seq += press(FIRE) + wait(20) + press(RIGHT) * (k % 3) + press(FIRE) + wait(70)
            k += 1
    elif name == "guard_lose":
        while len(seq) < frames:
            seq += press(LEFT) + press(FIRE) + wait(70)
    elif name == "cancel":
        while len(seq) < frames:
            seq += press(FIRE) + wait(20) + press(RIGHT) + press(B) + press(Y) + press(LEFT) + press(FIRE) + wait(70)
    elif name == "hold_a":
        # Hold A across turn boundaries -> exercises ui.lua idle-baseline latch.
        while len(seq) < frames:
            seq += [FIRE] * 90 + wait(5) + press(RIGHT) + [FIRE] * 40 + wait(30)
    elif name == "lock_edge":
        # Enter target mode, release, then re-press A at every offset around the
        # 0.25 s target-entry lock expiry; cancel with B and repeat.
        k = 8
        while len(seq) < frames:
            seq += press(FIRE, 2, 0) + wait(k) + [FIRE] * 4 + wait(3) + press(B) + wait(4)
            k = 8 if k >= 24 else k + 1
    elif name.startswith("field_"):
        T = 16                                              # ticks per tile (HERO_STEP_TICKS)
        def walk(btn, tiles, extra=0): return [btn] * (T * tiles + extra)
        def fight(loop, n):                                 # battle UI loop until n ticks used
            out = []
            while len(out) < n: out += loop
            return out[:n]
        # Battle loops use only A and Y, which do nothing on the field, so leftover loop
        # presses after a battle cannot move the hero (robust post-battle timing).
        attack = press(FIRE) + wait(20) + press(FIRE) + wait(70)     # Attack -> target -> confirm
        guard = press(Y) + press(FIRE) + wait(70)                    # Y jumps to Guard, A confirms
        to_slime = wait(20) + walk(RIGHT, 9) + walk(UP, 5, 4)        # spawn (3,10) -> (12,5), bump slime_a (12,4)
        to_guard_from_spawn = walk(RIGHT, 1) + walk(DOWN, 7) + walk(RIGHT, 6) + walk(UP, 0, 4)   # bump guard (10,16)
        if name == "field_walk":
            # (3,10) -L4-> wall at x=0 -U9-> (1,1) -L2-> wall -R2-> (3,1) -D3-> tree at (3,2) -R3-> (6,1)
            # -D4-> (6,5) -R12-> (18,5) -U12-> water at (19..,2) -D30-> bottom wall -L30-> left wall -R30-> right wall
            seq = wait(20) + walk(LEFT, 4) + walk(UP, 9) + walk(LEFT, 2) + walk(RIGHT, 2) + walk(DOWN, 3) + \
                  walk(RIGHT, 3) + walk(DOWN, 4) + walk(RIGHT, 12) + walk(UP, 12) + walk(DOWN, 30) + \
                  walk(LEFT, 30) + walk(RIGHT, 30)
        elif name == "field_win":
            # bump slime_a -> Attack until VICTORY -> A -> back at (12,5), slime_a gone -> walk on
            seq = to_slime + fight(attack, 1500) + wait(60) + walk(DOWN, 3) + walk(RIGHT, 4)
        elif name == "field_lose":
            # bump the guard in the doorway (10,16) -> Guard until DEFEAT -> A -> GAME OVER -> A -> restart
            seq = wait(20) + to_guard_from_spawn + fight(guard, 1600) + wait(60) + walk(RIGHT, 3)
        elif name == "field_full":
            # win vs slime_a, then walk (12,5) -R1-> (13,5) -D12-> (13,17) -L3-> (10,17), bump the guard, lose,
            # GAME OVER, retry: spawn, full HP and slime_a restored (F9 with a previously defeated foe)
            seq = to_slime + fight(attack, 1500) + wait(60) + walk(RIGHT, 1) + walk(DOWN, 12) + walk(LEFT, 3) + \
                  walk(UP, 0, 4) + fight(guard, 1600) + wait(60) + walk(RIGHT, 2)
        else:  # field_fuzz: random walks + button mashing (battles, cancels, retries)
            dirs = [UP, DOWN, LEFT, RIGHT]
            while len(seq) < frames:
                r = rng.random()
                if r < 0.55: seq += [rng.choice(dirs)] * rng.randint(4, 80)
                elif r < 0.85: seq += press(rng.choice([FIRE, RIGHT, LEFT, B, Y]), rng.randint(1, 6), rng.randint(0, 20))
                else: seq += wait(rng.randint(1, 40))
    elif name == "fuzz":
        buttons = [FIRE, LEFT, RIGHT, B, Y, UP, DOWN, START, FIRE | RIGHT, FIRE | LEFT, 0]
        while len(seq) < frames:
            if rng.random() < 0.7:
                b = rng.choice(buttons)
                seq += [b] * rng.randint(1, 25) + [0] * rng.randint(0, 30)
            else:
                # chords + overlapping holds: a random subset of buttons changes each frame
                for _ in range(rng.randint(5, 40)):
                    b = 0
                    for bit in (FIRE, LEFT, RIGHT, B, Y):
                        if rng.random() < 0.3: b |= bit
                    seq += [b] * rng.randint(1, 4)
    else:
        raise SystemExit("bad scenario")
    return seq[:frames]

def main():
    name, mode, seed, frames, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
    rng = random.Random(seed)
    seq = scenario(name, rng, frames)
    dts = dt_stream(mode, random.Random(seed ^ 0x5A5A))
    with open(out, "w") as f:
        f.write(f"# {name} {mode} seed={seed}\n")
        for b in seq:
            f.write("%.9g %02x\n" % (next(dts), b))

main()
