#!/usr/bin/env bash
# Runs the full differential suite: scripted scenarios x dt modes + fuzz seeds.
# usage: run_suite.sh <cpp_trace_binary> [tol] [alpha_tol] [fuzz_seeds]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROMDISK="${ROMDISK:-$HERE/../../zip/romdisk}"
PY="${PY:-python3}"   # interpreter with `pip install lupa`
BIN="$1"; TOL="${2:-1e-4}"; ATOL="${3:-0}"; SEEDS="${4:-40}"; EXTRA="${COMPARE_FLAGS:-}"
WORK="${WORK:-/tmp/suite}"; mkdir -p "$WORK"
pass=0; fail=0; failed=()

run_case() {  # name mode seed frames
  local id="$1.$2.$3" in="$WORK/$1.$2.$3.in"
  [ -f "$in" ] || python3 "$HERE/gen_inputs.py" "$1" "$2" "$3" "$4" "$in"
  [ -f "$in.lua" ] || "$PY" "$HERE/lua_ref.py" "$ROMDISK" "$in" "$in.lua"
  "$BIN" "$in" "$in.cpp"
  if out=$(python3 "$HERE/compare.py" "$in.lua" "$in.cpp" --tol "$TOL" --alpha-tol "$ATOL" --quiet $EXTRA); then
    pass=$((pass+1)); echo "  PASS $id  $out"
  else
    fail=$((fail+1)); failed+=("$id"); echo "  FAIL $id"; echo "$out" | sed 's/^/       /' | head -12
  fi
}

for sc in fireball_win attack_win guard_lose cancel hold_a lock_edge; do
  for mode in ms60 jitter dyadic; do run_case "$sc" "$mode" 1 2400; done
done
for s in $(seq 1 "$SEEDS"); do
  for mode in ms60 jitter dyadic; do run_case fuzz "$mode" "$s" 3000; done
done
echo "RESULT: pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
