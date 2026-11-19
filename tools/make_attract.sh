#!/usr/bin/env bash
# make_attract.sh - the attract demo (slice 3D-6, GDD 5; decisions D10, BA5): the story driver plays
# tests/barrow/routes/attract.route from a new game's start (the battle bot plays the patrol), the harness records every
# tick's buttons and stick (--record-inputs), and this packs them run-length into romdisk/demo/attract.in:
#   <ticks> <buttons hex> <stick x> <stick y>      (buttons: bit k = eng::Key k; one line per run of identical ticks)
# The game is deterministic (integer field logic, float == double), so the demo replays the same on every platform.
# Deterministic: the same tree makes the same file (tests/barrow/run.sh checks it). Needs `make host`.
#   tools/make_attract.sh [--check]
set -euo pipefail
cd "$(dirname "$0")/.."
W="${WORK:-/tmp/make_attract}"; mkdir -p "$W"
./build/battle_trace_f32 - /dev/null --state-only --route tests/barrow/routes/attract.route --max-ticks 8000 --record-inputs "$W/raw.in" > /dev/null 2>&1
python3 - "$W/raw.in" "$W/attract.in" <<'PYEOF'
import sys
rows = []
for ln in open(sys.argv[1]):
    p = ln.split()
    if len(p) < 2 or ln.startswith('#'):
        continue
    rows.append((int(p[1], 16), int(p[2]) if len(p) > 2 else 0, int(p[3]) if len(p) > 3 else 0))
out = ['# attract.in - the attract demo (tools/make_attract.sh from tests/barrow/routes/attract.route): %d ticks' % len(rows),
       '# <ticks> <buttons hex> <stick x> <stick y> per run of identical ticks (bit k = eng::Key k)']
k = 0
while k < len(rows):
    j = k
    while j < len(rows) and rows[j] == rows[k] and j - k < 65535:
        j += 1
    b, x, y = rows[k]
    out.append('%d %02x %d %d' % (j - k, b, x, y))
    k = j
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
PYEOF
if [ "${1:-}" = "--check" ]; then
  cmp -s "$W/attract.in" romdisk/demo/attract.in && echo "ok   romdisk/demo/attract.in" || { echo "DIFF romdisk/demo/attract.in"; exit 1; }
else
  cp "$W/attract.in" romdisk/demo/attract.in
  echo "wrote romdisk/demo/attract.in: $(grep -vc '^#' romdisk/demo/attract.in) runs, $(head -1 romdisk/demo/attract.in | grep -o '[0-9]* ticks')"
fi
