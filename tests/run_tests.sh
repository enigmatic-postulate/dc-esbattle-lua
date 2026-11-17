#!/usr/bin/env bash
# Slice test suite (DoD T6, T7, T8, F1-F9 automated parts, P2).
#   tests/run_tests.sh            run everything against tests/golden/
#   tests/run_tests.sh --update   re-record the golden traces (after an intentional change)
# Needs: g++, python3. Runs from any directory.
set -u
cd "$(dirname "$0")/.."
UPDATE=0; [ "${1:-}" = "--update" ] && UPDATE=1
WORK="${WORK:-/tmp/bt_tests}"; mkdir -p "$WORK"
pass=0; fail=0; failed=()
ok()   { pass=$((pass+1)); echo "  PASS $1"; }
bad()  { fail=$((fail+1)); failed+=("$1"); echo "  FAIL $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/       /' | head -8; }

echo "== build (host, float + double)"
make -s host >/dev/null || { echo "host build failed"; exit 1; }
SHZ_FLAGS=$(make -s print-host-shz-flags)   # SH4ZAM headers (software back-end), as the host build uses
warn=$(g++ -std=gnu++17 -Wall -Wextra -Wshadow $SHZ_FLAGS -fsyntax-only src/*.cpp 2>&1 | grep -c warning)
[ "$warn" = "0" ] && ok "T1 src builds with 0 warnings" || bad "T1 src warnings: $warn"

run_case() {   # name kind(battle|field) expect_wins expect_restarts
  local name="$1" kind="$2" in="tests/inputs/$1.in" gold="tests/golden/$1.trace.gz"
  # battle goldens: recorded from the sprite battle with the original rules (slice 3D-4 keeps both as
  # --view2d / --rules legacy; the 3D battle is checked turn by turn by tests/battle3d/run.sh)
  local args=(--state-only); [ "$kind" = battle ] && args+=(--battle-test test_slimes --view2d --rules legacy)
  # field goldens (slice 3D-5 triage): the test map they were written for, with the sprite battle and the original
  # rules; field_win / field_lose / field_full were regenerated with the story driver (tests/inputs/routes/), as the
  # analog hero no longer follows the old tile-stepping key scripts
  [ "$kind" = field ] && args+=(--map /rd/maps/test_field.txt --view2d --rules legacy)
  ./build/battle_trace_f64 "$in" "$WORK/$name.64" "${args[@]}" || { bad "$name (run f64)"; return; }
  ./build/battle_trace_f32 "$in" "$WORK/$name.32" "${args[@]}" || { bad "$name (run f32)"; return; }
  if [ $UPDATE = 1 ]; then gzip -9 -n -c "$WORK/$name.64" > "$gold"; fi
  if out=$(python3 harness/compare.py "$gold" "$WORK/$name.64" --state-only --quiet); then ok "$name golden ($out)"; else bad "$name golden" "$out"; fi
  if cmp -s "$WORK/$name.64" "$WORK/$name.32"; then ok "$name T7 float==double"; else bad "$name T7 float!=double"; fi
  if [ "$kind" = field ]; then
    local ex=(); [ -n "${3:-}" ] && ex+=(--expect-wins "$3"); [ -n "${4:-}" ] && ex+=(--expect-restarts "$4")
    if out=$(python3 tests/check_invariants.py "$WORK/$name.64" "${ex[@]}"); then ok "$name invariants"; else bad "$name invariants" "$out"; fi
  fi
}

echo "== battle regression (battle-test mode, encounter test_slimes)"
for c in fireball_win attack_win guard_lose cancel hold_a lock_edge fuzz1 fuzz2 fuzz3 fuzz4 fuzz5 fuzz6; do run_case "battle_$c" battle; done

echo "== battle UI buttons (F7: A / B-cancel / Y-shortcut)"
./build/battle_trace_f64 tests/inputs/battle_ui_buttons.in "$WORK/ui_buttons.full" --battle-test test_slimes --view2d --rules legacy
if out=$(python3 tests/check_ui_buttons.py "$WORK/ui_buttons.full"); then ok "$out"; else bad "ui buttons" "$out"; fi

echo "== field (goldens + invariants F3/F4/F8/F9 + expected outcomes)"
run_case field_walk field 0 0
run_case field_win  field 1 0
run_case field_lose field 0 1
run_case field_full field 1 1
for s in 1 2 3 4 5; do run_case "field_fuzz$s" field; done

echo "== P2 assets reproducible from tools/make_assets.py"
# The generator writes into romdisk/ and docs/: keep a copy and put the committed files back
# afterwards, so a differing local PIL can never change the game's content.
snap="$WORK/p2_snapshot"; rm -rf "$snap"; mkdir -p "$snap/romdisk" "$snap/docs"
cp -p romdisk/*.png "$snap/romdisk/"; cp -p docs/*.png "$snap/docs/"
before=$(md5sum romdisk/*.png | sort)
python3 tools/make_assets.py >/dev/null 2>&1
after=$(md5sum romdisk/*.png | sort)
cp -p "$snap"/romdisk/*.png romdisk/; cp -p "$snap"/docs/*.png docs/
[ "$before" = "$after" ] && ok "P2 committed PNGs == generator output" || bad "P2 assets differ from generator output"

echo "RESULT: pass=$pass fail=$fail"
[ $fail -eq 0 ]
