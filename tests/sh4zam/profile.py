#!/usr/bin/env python3
"""Attributes a shcount.c block profile (prof=FILE) to functions.

    python3 tests/sh4zam/profile.py BENCH.elf PROFILE [N]

Prints the N functions that executed the most SH4 instructions over the whole run (C++ names
demangled), using the sh-elf binutils' nm and c++filt (set NM / CXXFILT to override).
"""
import bisect
import os
import subprocess
import sys


def main():
    elf, prof = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    nm = os.environ.get('NM', 'sh-elf-nm')
    cxxfilt = os.environ.get('CXXFILT', 'sh-elf-c++filt')
    syms = []
    for line in subprocess.run([nm, '-n', elf], capture_output=True, text=True).stdout.splitlines():
        t = line.split()
        if len(t) == 3 and t[1] in 'tTwW':
            syms.append((int(t[0], 16), t[2]))
    syms.sort()
    addrs = [a for a, _ in syms]
    per = {}
    total = 0
    for line in open(prof):
        pc, n, ex = line.split()
        cost = int(n) * int(ex)
        if not cost:
            continue
        i = bisect.bisect_right(addrs, int(pc, 16)) - 1
        name = syms[i][1] if i >= 0 else '?'
        per[name] = per.get(name, 0) + cost
        total += cost
    names = sorted(per, key=lambda k: -per[k])[:top]
    dem = subprocess.run([cxxfilt, '-_'], input='\n'.join(names), capture_output=True, text=True).stdout.splitlines()
    print(f'total {total:,} instructions')
    for k, d in zip(names, dem):
        print(f'{per[k]:14,d} {100.0 * per[k] / total:5.1f}%  {d[:110]}')


if __name__ == '__main__':
    main()
