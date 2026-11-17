#!/usr/bin/env bash
# Negative fixtures (tests/dialog/lint_bad/*.txt): each must be refused by tools/dialog/lint.py with its
# '# expect:' message; those marked '# cpp: reject' must also be refused by the game's compiler at load
# (src/script.cpp: the game shows SCRIPT ERROR and logs the same message - a load-time error, never a crash),
# those marked 'accept' compile (the check is the lint's analysis only).
set -u
cd "$(dirname "$0")/../.."
W="${WORK:-/tmp/dialog_tests}/lint_bad"; rm -rf "$W"; mkdir -p "$W"
BIN="${BIN:-./build/battle_trace_f32}"
pass=0; fail=0
for f in tests/dialog/lint_bad/*.txt; do
  n=$(basename "$f" .txt)
  exp=$(sed -n 's/^# expect: //p' "$f"); cpp=$(sed -n 's/^# cpp: //p' "$f")
  out=$(python3 tools/dialog/lint.py --script "$f" --quiet 2>&1); rc=$?
  if [ $rc = 0 ] || ! echo "$out" | grep -qF "$exp"; then echo "  lint did not refuse $n with '$exp': $out" | head -3; fail=$((fail+1)); continue; fi
  rd="$W/rd_$n"; mkdir -p "$rd"; cp -r romdisk/. "$rd/"; cp "$f" "$rd/scripts/village.txt"
  printf '0.0166667 00\n0.0166667 00\n' > "$W/two.in"
  timeout 30 "$BIN" "$W/two.in" "$W/$n.trace" --state-only --romdisk "$rd" > /dev/null 2>&1; crc=$?
  if [ $crc -gt 1 ]; then echo "  the game crashed on $n (exit $crc)"; fail=$((fail+1)); continue; fi
  if [ "$cpp" = reject ]; then
    if ! grep -q "^X mode=error" "$W/$n.trace" || ! grep "^D script" "$W/$n.trace" | grep -qF "$exp"; then
      echo "  the game's compiler did not refuse $n with '$exp': $(grep '^D script' "$W/$n.trace" | head -1)"; fail=$((fail+1)); continue; fi
  else
    if ! grep -q "^X mode=field" "$W/$n.trace"; then echo "  the game's compiler refused $n (lint-only check): $(grep '^D script' "$W/$n.trace" | head -1)"; fail=$((fail+1)); continue; fi
  fi
  pass=$((pass+1))
done
echo "lint fixtures: $pass refused as expected, $fail wrong"
[ $fail = 0 ]
