"""strips.py - greedy triangle stripifier for .dmf batches (winding-exact).

A strip s0 s1 s2 s3 ... encodes triangles (s0 s1 s2), (s2 s1 s3), (s2 s3 s4), ... : odd triangles
swap their first two corners, so every triangle keeps the winding it had in the input (the PVR
culls single-sided batches by winding). No degenerate stitching: strips are separate.

Greedy: start from the unused triangle with the fewest unused neighbours, try its three
rotations, extend forward as far as possible, keep the longest. Deterministic.
"""
from collections import defaultdict


def _key(a, b):
    return (a, b) if a < b else (b, a)


def stripify(tris):
    """tris: list of (a, b, c) vertex indices, counter-clockwise. Returns a list of strips."""
    tris = [tuple(t) for t in tris if len({t[0], t[1], t[2]}) == 3]
    n = len(tris)
    edge_tris = defaultdict(list)
    for i, (a, b, c) in enumerate(tris):
        for e in (_key(a, b), _key(b, c), _key(c, a)):
            edge_tris[e].append(i)
    used = [False] * n

    def neighbours(i):
        a, b, c = tris[i]
        out = set()
        for e in (_key(a, b), _key(b, c), _key(c, a)):
            out.update(j for j in edge_tris[e] if j != i and not used[j])
        return out

    def find_next(x, y, parity):
        """Unused triangle containing edge (x, y) whose third vertex z makes the strip step valid:
        even step -> triangle (x, y, z) in input winding; odd step -> (y, x, z)."""
        for j in edge_tris[_key(x, y)]:
            if used[j]:
                continue
            a, b, c = tris[j]
            rots = ((a, b, c), (b, c, a), (c, a, b))
            want = (x, y) if parity == 0 else (y, x)
            for r in rots:
                if (r[0], r[1]) == want:
                    return j, r[2]
        return None

    def grow(start, rot):
        a, b, c = tris[start]
        s = [(a, b, c), (b, c, a), (c, a, b)][rot]
        strip = list(s)
        taken = [start]
        used[start] = True
        while True:
            j = len(strip) - 2                       # index of the next triangle in the strip
            nxt = find_next(strip[-2], strip[-1], j & 1)
            if nxt is None:
                break
            t, z = nxt
            used[t] = True
            taken.append(t)
            strip.append(z)
        for t in taken:                              # undo (the caller commits the best)
            used[t] = False
        return strip, taken

    strips = []
    remaining = n
    while remaining:
        best_i, best_deg = None, 99
        for i in range(n):
            if not used[i]:
                d = len(neighbours(i))
                if d < best_deg:
                    best_i, best_deg = i, d
                    if d <= 1:
                        break
        best = None
        for rot in range(3):
            s, taken = grow(best_i, rot)
            if best is None or len(taken) > len(best[1]):
                best = (s, taken)
        for t in best[1]:
            used[t] = True
        remaining -= len(best[1])
        strips.append(best[0])
    return strips


def check(tris, strips):
    """True if the strips encode exactly the input triangles with the same winding."""
    def canon(t):
        a, b, c = t
        m = min(range(3), key=lambda k: t[k])
        return t[m:] + t[:m]
    want = sorted(canon(tuple(t)) for t in tris if len(set(t)) == 3)
    got = []
    for s in strips:
        for j in range(len(s) - 2):
            a, b, c = s[j], s[j + 1], s[j + 2]
            if j & 1:
                a, b = b, a
            got.append(canon((a, b, c)))
    return sorted(got) == want
