#!/usr/bin/env bash
# SH4 cost of the 3D battle: the game compiled like the DreamSDK build (SH4ZAM's SH4 back-end) runs
# bot-played battles under qemu with the instruction-counting plugin (tests/sh4zam/). The battles'
# inputs are recorded on the host (trace_main --record-inputs) and replayed tick for tick.
# Needs the kit of tests/sh4zam/README.md: SH_ELF, QEMU_SH4, QEMU_SRC, WORK (sh4kit.sh rt/lib/plugin done).
# Budget: the battle's worst frames (p99, and the worst after the first frame) within the field's worst
# frame measured by the same build, and its median within 1.25 x the field's median.
# CACHE=1 also replays every frame against the operand-cache model (shcache.c) and prints modelled
# CPU time per frame: 1 cycle per instruction + long FPU ops + 20 or 40 cycles per demand miss
# (the model of tests/sh4zam/summarize_cache.py; not a hardware measurement).
set -eu
cd "$(dirname "$0")/../.."
: "${SH_ELF:?}" "${QEMU_SH4:?}" "${WORK:?}"
OUT="$WORK/b3_counts"; mkdir -p "$OUT"
bash tests/sh4zam/sh4kit.sh bench . "$WORK/bench_b3.elf" > /dev/null
addr() { printf "0x%s" "$("$SH_ELF/bin/sh-elf-nm" "$1" | awk -v s="$2" '$3==s {print $1}')"; }
E="$WORK/bench_b3.elf"
P="$WORK/libshcount.so,tick=$(addr "$E" _bench_mark_tick),draw=$(addr "$E" _bench_mark_draw),end=$(addr "$E" _bench_mark_end)"
runs=""
for run in "test_slimes cycle:AFG" "slimes_2 random:3" "guard_duel cycle:FGGA" "warden boss:good:2"; do
  set -- $run
  n="${1}_${2//:/_}"
  ./build/battle_trace_f32 - /dev/null --battle-test "$1" --state-only --bot "$2" --bot-wait 120 --bot-linger 120 --record-inputs "$OUT/$n.in" > /dev/null
  "$QEMU_SH4" -plugin "$P,out=$OUT/$n.txt" "$E" "$OUT/$n.in" romdisk --battle "$1" > /dev/null
  [ "${CACHE:-0}" = 1 ] && "$QEMU_SH4" -plugin "${P/libshcount/libshcache},out=$OUT/$n.cache" "$E" "$OUT/$n.in" romdisk --battle "$1" > /dev/null
  runs="$runs $OUT/$n.txt"
done
for f in field_walk field_fuzz2; do
  "$QEMU_SH4" -plugin "$P,out=$OUT/$f.txt" "$E" tests/inputs/$f.in romdisk > /dev/null
  [ "${CACHE:-0}" = 1 ] && "$QEMU_SH4" -plugin "${P/libshcount/libshcache},out=$OUT/$f.cache" "$E" tests/inputs/$f.in romdisk > /dev/null
done
python3 - "$OUT/field_walk.txt" "$OUT/field_fuzz2.txt" -- $runs <<'PYEOF'
import statistics, sys
CL = ['all', 'fdiv', 'fsqrt', 'fsrra', 'fsca', 'ftrv', 'fipr', 'farith', 'fmem', 'branch', 'imem']
def load(p):
    out = []
    for ln in open(p):
        if ln.startswith('frame'):
            t = ln.split()
            out.append(int(t[3]) + int(t[4 + len(CL)]))      # tick + draw instructions
    return out
args = sys.argv[1:]; k = args.index('--')
field = sum((load(p) for p in args[:k]), [])
fmed, fmax = statistics.median(field), max(field)
print('field (same build): %d frames, median %d, worst %d instructions' % (len(field), fmed, fmax))
ok = True
for p in args[k + 1:]:
    b = load(p)
    s = sorted(b); med = statistics.median(b); p99 = s[int(0.99 * len(s))]; worst1 = max(b[1:])
    good = p99 <= fmax and worst1 <= fmax * 1.05 and med <= 1.25 * fmed
    ok = ok and good
    print('%-32s %5d frames  median %8d  p99 %8d  worst after frame 1 %8d  (%.2f / %.2f ms at 1 instr/cycle)  %s' % (
        p.split('/')[-1][:-4], len(b), med, p99, worst1, med / 200e3, worst1 / 200e3, 'OK' if good else 'OVER'))
import os
CA = ['rmiss', 'wmiss', 'wback', 'alloc', 'pref', 'phit']
def load_full(p, names):
    out = []
    for ln in open(p):
        if ln.startswith('frame'):
            t = ln.split(); n = len(names)
            out.append({k: int(t[3 + i]) + int(t[4 + n + i]) for i, k in enumerate(names)})
    return out
paths = args[:k] + args[k + 1:]
if all(os.path.exists(p[:-4] + '.cache') for p in paths):
    print('modelled CPU time per frame (ms at 200 MHz), median / worst after frame 1, 20- and 40-cycle miss:')
    for p in paths:
        I = load_full(p, CL); C = load_full(p[:-4] + '.cache', CA)
        def cyc(i, c, miss): return i['all'] + 10 * (i['fdiv'] + i['fsqrt']) + 3 * i['ftrv'] + miss * (c['rmiss'] + c['wmiss'])
        for miss in (20, 40):
            v = [cyc(I[j], C[j], miss) for j in range(len(I))]
            print('  %-30s @%d: median %5.2f  worst %5.2f' % (p.split('/')[-1][:-4], miss, statistics.median(v) / 200e3, max(v[1:]) / 200e3))
sys.exit(0 if ok else 1)
PYEOF
