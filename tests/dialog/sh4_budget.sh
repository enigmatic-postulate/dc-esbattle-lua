#!/usr/bin/env bash
# SH4 cost of the dialog slice (3D-5): the game compiled like the DreamSDK build (SH4ZAM's SH4 back-end) replays
# the canonical story route (every dialog of the quest, the three battles) and the dialog UI route under qemu with
# the instruction-counting plugin (tests/sh4zam/). The inputs are recorded on the host (the story driver and the
# battle bot, --record-inputs) and replayed tick for tick; the host trace of the same run gives each tick's mode and
# the portrait uploads, so frames are classified as field, dialog and portrait-swap frames.
# Needs the kit of tests/sh4zam/README.md: SH_ELF, QEMU_SH4, QEMU_SRC, WORK (sh4kit.sh rt/lib/plugin done).
# Budgets (docs/NEXT_SLICE_DIALOG.md 9, T1): field worst <= 1.40 M instructions; dialog frames and portrait-swap
# frames <= the field's worst; the median dialog frame within 1.25 x the median field frame.
# Not modelled by the bench (its engine is a no-op except the vertex arena): the KOS sprite submission
# (~200 instructions per 2D sprite, a dialog frame has at most ~160) and the portrait copy (32 KiB, pvr_txr_load).
set -eu
cd "$(dirname "$0")/../.."
: "${SH_ELF:?}" "${QEMU_SH4:?}" "${WORK:?}"
OUT="$WORK/dlg_counts"; mkdir -p "$OUT"
bash tests/sh4zam/sh4kit.sh bench . "$WORK/bench_dlg.elf" > /dev/null
addr() { printf "0x%s" "$("$SH_ELF/bin/sh-elf-nm" "$1" | awk -v s="$2" '$3==s {print $1}')"; }
E="$WORK/bench_dlg.elf"
P="$WORK/libshcount.so,tick=$(addr "$E" _bench_mark_tick),draw=$(addr "$E" _bench_mark_draw),end=$(addr "$E" _bench_mark_end)"
RU="$OUT/rd_ui"; rm -rf "$RU"; cp -r romdisk "$RU"; cp tests/dialog/fixtures/ui_village.txt "$RU/scripts/village.txt"
./build/battle_trace_f32 - "$OUT/r1.trace" --state-only --route tests/dialog/routes/r1_canonical.route --max-ticks 90000 --record-inputs "$OUT/r1.in" > /dev/null 2>&1
./build/battle_trace_f32 - "$OUT/ui.trace" --state-only --romdisk "$RU" --route tests/dialog/ui.route --max-ticks 3000 --record-inputs "$OUT/ui.in" > /dev/null 2>&1
"$QEMU_SH4" -plugin "$P,out=$OUT/r1.txt" "$E" "$OUT/r1.in" romdisk > /dev/null
"$QEMU_SH4" -plugin "$P,out=$OUT/ui.txt" "$E" "$OUT/ui.in" "$RU" > /dev/null
for f in field_walk field_fuzz2; do "$QEMU_SH4" -plugin "$P,out=$OUT/$f.txt" "$E" tests/inputs/$f.in romdisk > /dev/null; done
python3 - "$OUT" <<'PYEOF'
import statistics, sys, os
OUT = sys.argv[1]
CL = ['all', 'fdiv', 'fsqrt', 'fsrra', 'fsca', 'ftrv', 'fipr', 'farith', 'fmem', 'branch', 'imem']
def counts(p):
    out = []
    for ln in open(p):
        if ln.startswith('frame'):
            t = ln.split()
            out.append(int(t[3]) + int(t[4 + len(CL)]))      # tick + draw instructions
    return out
def modes(trace):
    """mode per tick (1-based) and the ticks where a portrait was uploaded, from the host trace of the same run"""
    mode, cur, uploads, prev_up, m = {}, 'field', set(), 0, {}
    events = []
    for ln in open(trace):
        p = ln.split()
        if p and p[0] == 'N':
            events.append((int(p[1]), p[2]))
        elif p and p[0] == 'G':
            up = int([kv for kv in p if kv.startswith('uploads=')][0].split('=')[1])
            if up > prev_up: uploads.add(int(p[1]))
            prev_up = up
    last = max([t for t, _ in events] + [1])
    ev = dict(events)
    for t in range(1, last + 200000):
        if t in ev: cur = ev[t]
        m[t] = cur
    return m, uploads
field = counts(f'{OUT}/field_walk.txt') + counts(f'{OUT}/field_fuzz2.txt')
fmed, fmax = statistics.median(field), max(field)
print('field (tests/inputs/field_walk + field_fuzz2, this build): %d frames, median %.3f M, worst %.3f M instructions' % (len(field), fmed / 1e6, fmax / 1e6))
ok = fmax <= 1.40e6
for name, trace in (('r1', 'r1.trace'), ('ui', 'ui.trace')):
    c = counts(f'{OUT}/{name}.txt')
    m, up = modes(f'{OUT}/{trace}')
    for mode in ('field', 'dialog'):
        v = [c[i] for i in range(len(c)) if m.get(i + 1) == mode]
        if not v: continue
        s = sorted(v)
        print('%-3s %-7s %5d frames: median %.3f M, p99 %.3f M, worst %.3f M' % (name, mode, len(v), statistics.median(v) / 1e6, s[int(0.99 * len(s))] / 1e6, max(v) / 1e6))
        if mode == 'field': ok = ok and max(v) <= 1.40e6
        if mode == 'dialog': ok = ok and max(v) <= max(fmax, 1.0) and statistics.median(v) <= 1.25 * fmed
    sw = [c[t - 1] for t in sorted(up) if t - 1 < len(c)]
    if sw:
        print('%-3s portrait-swap frames: %d, worst %.3f M (+ the 32 KiB copy on KOS)' % (name, len(sw), max(sw) / 1e6))
        ok = ok and max(sw) <= fmax
print('SH4 budget:', 'OK' if ok else 'OVER')
sys.exit(0 if ok else 1)
PYEOF
