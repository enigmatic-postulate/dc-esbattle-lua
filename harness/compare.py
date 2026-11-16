#!/usr/bin/env python3
"""
Differential comparison of a Lua reference trace vs a C++ port trace.

Per frame, the ordered sequence of engine calls must match:
  S (sprite draw): same sprite path + filter, numeric args within --tol
  R (rect):        same list, RGB exact, alpha within --alpha-tol, numeric args within --tol
  D (debug print): exact
  X (logic state): exact (state machine, active actor, turn index, order, HP, statuses)
'L' (sprite load) lines are excluded: the port de-duplicates texture loads by design.

Usage: compare.py ref.trace port.trace [--tol 1e-4] [--alpha-tol 0] [--quiet]
Exit code 0 == equivalent.
"""
import sys, argparse, gzip

ap = argparse.ArgumentParser()
ap.add_argument("ref"); ap.add_argument("port")
ap.add_argument("--tol", type=float, default=1e-4)
ap.add_argument("--alpha-tol", type=int, default=0)
ap.add_argument("--quiet", action="store_true")
ap.add_argument("--state-only", action="store_true",
                help="compare only logic snapshots (X) and debug prints (D); use after intentional presentation changes")
a = ap.parse_args()

def load(path):
    frames, loads, cur = [], 0, None
    for line in (gzip.open(path, 'rt') if path.endswith('.gz') else open(path)):
        line = line.rstrip("\n")
        if not line: continue
        if line.startswith("F "):
            cur = [line.split()[1], []]; frames.append(cur); continue
        if line.startswith("L "): loads += 1; continue
        if a.state_only and not line.startswith(("X ", "D ")): continue
        cur[1].append(line)
    return frames, loads

ref, ref_loads = load(a.ref)
port, port_loads = load(a.port)

max_err = 0.0
max_alpha = 0
ncalls = 0

def cmp_line(r, p):
    global max_err, max_alpha
    rt, pt = r.split(), p.split()
    if rt[0] != pt[0] or len(rt) != len(pt): return "kind/arity"
    k = rt[0]
    if k in ("X", "D"):
        return None if r == p else "text"
    if k == "S":
        if rt[1] != pt[1]: return "sprite"
        if rt[7] != pt[7]: return "filter"
        nums = list(range(2, 7)) + list(range(8, len(rt)))
    else:  # R
        if rt[1] != pt[1]: return "list"
        ra, pa = int(rt[6], 16), int(pt[6], 16)
        if (ra & 0xFFFFFF) != (pa & 0xFFFFFF): return "rgb"
        da = abs((ra >> 24) - (pa >> 24)); max_alpha = max(max_alpha, da)
        if da > a.alpha_tol: return "alpha"
        nums = [2, 3, 4, 5, 7]
    for i in nums:
        e = abs(float(rt[i]) - float(pt[i]))
        max_err = max(max_err, e)
        if e > a.tol: return f"field{i} |d|={e:.3g}"
    return None

def fail(fi, idx, why):
    fr, fp = ref[fi], port[fi]
    print(f"DIVERGENCE at frame {fr[0]} call #{idx}: {why}")
    lo = max(0, idx - 3)
    for j in range(lo, min(idx + 4, max(len(fr[1]), len(fp[1])))):
        rl = fr[1][j] if j < len(fr[1]) else "<none>"
        pl = fp[1][j] if j < len(fp[1]) else "<none>"
        mark = ">>" if j == idx else "  "
        print(f" {mark} lua: {rl}\n {mark} cpp: {pl}")
    sys.exit(1)

if len(ref) != len(port):
    print(f"frame count differs: lua={len(ref)} cpp={len(port)}"); sys.exit(1)
for fi, (fr, fp) in enumerate(zip(ref, port)):
    if fr[0] != fp[0]: print(f"frame header mismatch {fr[0]} vs {fp[0]}"); sys.exit(1)
    n = max(len(fr[1]), len(fp[1]))
    for j in range(n):
        if j >= len(fr[1]) or j >= len(fp[1]): fail(fi, j, "call count differs")
        why = cmp_line(fr[1][j], fp[1][j])
        if why: fail(fi, j, why)
        ncalls += 1

if not a.quiet:
    print(f"{'STATE-' if a.state_only else ''}EQUIVALENT: {len(ref)} frames, {ncalls} calls, max |d|={max_err:.3g}, "
          f"max alpha d={max_alpha}, sprite loads lua={ref_loads} cpp={port_loads}")
else:
    print(f"OK {len(ref)} frames {ncalls} calls max|d|={max_err:.3g} alpha_d={max_alpha}")
