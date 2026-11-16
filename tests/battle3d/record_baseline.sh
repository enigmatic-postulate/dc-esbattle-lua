#!/usr/bin/env bash
# Records the turn-level baselines (tests/battle3d/baseline/) from a build of the ORIGINAL sprite
# battle. Run once, from a checkout of the pre-3D tree with platform/host/battle_bot.cpp and the
# --bot options added to its trace_main.cpp (see tests/battle3d/README.md):
#   tests/battle3d/record_baseline.sh <pre-3D tree> [out dir]
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:?pre-3D tree}"; OUT="${2:-$HERE/baseline}"
mkdir -p "$OUT"
for enc in test_slimes slimes_2 guard_duel; do
  grep -v '^#' "$HERE/policies.txt" | while read -r pol; do
    [ -z "$pol" ] && continue
    f=$(echo "${enc}__${pol}" | tr ':' '_')
    "$SRC/build/battle_trace_f64" - /dev/null --battle-test "$enc" --state-only --bot "$pol" --turn-log "$OUT/$f.log"
    "$SRC/build/battle_trace_f32" - /dev/null --battle-test "$enc" --state-only --bot "$pol" --turn-log "$OUT/$f.f32"
    cmp -s "$OUT/$f.log" "$OUT/$f.f32" || echo "baseline float != double: $f"
    rm -f "$OUT/$f.f32"
  done
done
echo "recorded $(ls "$OUT"/*.log | wc -l) baselines in $OUT"
