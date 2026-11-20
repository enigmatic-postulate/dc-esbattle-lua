#!/usr/bin/env bash
# Slice 3D-5 ("Talk to Wren": dialog, portraits, the village quest) test suite. Runs from any directory; needs
# g++, python3 (numpy, pillow, fontTools) and an SH4ZAM checkout (as `make host`).
#   tests/dialog/run.sh            everything below (about 8 minutes)
#   QUICK=1 tests/dialog/run.sh    skips the sanitizer build and the legacy baselines (about 3 minutes)
#   BPY=<bpy python> ...           also rebuilds the Blender-made assets (Wren, Brom, portraits, the library)
#                                  and checks they are byte-identical
#   SH4=1 ...                      also measures SH4 instructions per frame under qemu (tests/dialog/sh4_budget.sh;
#                                  the toolchain of tests/sh4zam/README.md)
# The slice 3D-4 suite (tests/battle3d/run.sh) and the older one (tests/run_tests.sh) keep running separately.
set -u
cd "$(dirname "$0")/../.."
W="${WORK:-/tmp/dialog_tests}"; mkdir -p "$W"
Q="${QUICK:-0}"
pass=0; fail=0; failed=()
ok()  { pass=$((pass+1)); echo "  PASS $1"; }
bad() { fail=$((fail+1)); failed+=("$1"); echo "  FAIL $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/       /' | head -12; }
T() { timeout "${TMO:-180}" "$@"; }
B32=./build/battle_trace_f32; B64=./build/battle_trace_f64
ROUTES="r1_canonical r2_patrols_first r3_skip_brom r4_everyone_twice r5_loops r6_lose_retry cp_s0 cp_s3"

echo "== build (host, float + double) and warnings"
make -s host >/dev/null 2>&1 || { echo "host build failed"; exit 1; }
SHZ=$(make -s print-host-shz-flags)
warn=$(g++ -std=gnu++17 -Wall -Wextra -Wshadow $SHZ -fsyntax-only src/*.cpp 2>&1 | grep -c warning)
wopt=0; for f in src/*.cpp platform/host/*.cpp; do c=$(g++ -std=gnu++17 -O2 -Wall -Wextra -Wshadow $SHZ -c "$f" -o "$W/w.o" 2>&1 | grep -c "warning\|error"); wopt=$((wopt+c)); done
[ "$warn" = 0 ] && [ "$wopt" = 0 ] && ok "src and the host harness build with 0 warnings (-Wall -Wextra -Wshadow, also at -O2)" || bad "warnings: $warn (syntax) + $wopt (-O2)"
kw=0; for d in "" "-DBATTLE_DIAG=1" "-DINPUT_OVERLAY=1 -DR3D_SAFE=1" "-DPVR_DEFAULTS=1 -DGROUND_NO_VQ=1"; do
  c=$(g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wshadow -Itests/dialog/kos_stub $SHZ $d platform/kos/main.cpp 2>&1 | grep -c "warning\|error"); kw=$((kw+c)); done
[ "$kw" = 0 ] && ok "the KOS backend compiles against stub KOS headers (syntax and types only; 4 build flavours)" || bad "KOS backend stub check: $kw warnings/errors"

echo "== lint (GDD 7.4) and its negative fixtures"
if out=$(python3 tools/dialog/lint.py); then ok "$out"; else bad "lint on the shipped scripts" "$out"; fi
if out=$(bash tests/dialog/check_lint_fixtures.sh 2>&1); then ok "$(echo "$out" | tail -1) (lint and the game's compiler; pool overflows are load-time errors)"; else bad "lint fixtures" "$out"; fi

echo "== parity: the game's compiler == lint.py, the game's text metrics == lint.py"
python3 tools/dialog/lint.py --dump > "$W/py.dump"; T $B32 - /dev/null --dump-scripts > "$W/cpp.dump"
if cmp -s "$W/py.dump" "$W/cpp.dump"; then ok "compiled scripts identical ($(grep -c '^[0-9]' "$W/cpp.dump") ops, wrapped pages, conditions, markers)"; else bad "compiled scripts differ" "$(diff "$W/py.dump" "$W/cpp.dump" | head -6)"; fi
if out=$(python3 tests/dialog/check_widths.py $B32); then ok "$out"; else bad "text widths" "$out"; fi

echo "== the talk prompt (1.3 tiles, 60 degrees, nearest, ties) and the dialog camera (T-CAM)"
T $B32 - /dev/null --probe-talk > "$W/probe.txt"
if out=$(python3 tests/dialog/check_talk.py "$W/probe.txt" romdisk/maps/village.txt); then ok "$out"; else bad "talk prompt (village)" "$out"; fi
RT="$W/rd_talk"; rm -rf "$RT"; cp -r romdisk "$RT"; cp tests/dialog/fixtures/talk_map.txt "$RT/maps/talk_map.txt"
T $B32 - /dev/null --probe-talk --romdisk "$RT" --map /rd/maps/talk_map.txt > "$W/probe2.txt"
if out=$(python3 tests/dialog/check_talk.py "$W/probe2.txt" tests/dialog/fixtures/talk_map.txt) && echo "$out" | grep -q "([1-9][0-9]* exact ties)"; then
  ok "$out"; else bad "talk prompt (two NPCs side by side)" "$out"; fi
T $B32 - /dev/null --probe-cam > "$W/cam.txt"
n_ok=$(awk '$5==1' "$W/cam.txt" | wc -l); n_all=$(wc -l < "$W/cam.txt")
if [ "$n_all" -gt 0 ] && [ $((n_ok * 100 / n_all)) -ge 90 ]; then
  ok "two-shot eye outside every prop box from $n_ok of $n_all talk positions; the field camera for the rest: $(awk '$5==0{printf "%s@%s,%s ", $2,$3,$4}' "$W/cam.txt")"
else bad "dialog camera validity: $n_ok of $n_all"; fi

echo "== the dialog UI (T3/T4, typewriter, A/B, choices, portraits, fades) on scripted inputs"
RU="$W/rd_ui"; rm -rf "$RU"; cp -r romdisk "$RU"; cp tests/dialog/fixtures/ui_village.txt "$RU/scripts/village.txt"
T $B32 - "$W/ui.trace" --state-only --romdisk "$RU" --route tests/dialog/ui.route --max-ticks 3000 > /dev/null 2>&1
python3 tools/dialog/lint.py "$RU" --dump > "$W/ui.dump"
if out=$(python3 tests/dialog/check_dialog.py "$W/ui.trace" "$W/ui.dump" --expect-sessions 2 \
          --require T3-bars,T3-box,T3-cam,T4,type,A-complete,A-next,marker,choice,consume,hud,portrait,fade); then ok "$out"; else bad "dialog UI" "$out"; fi

TK=$(python3 tests/dialog/check_layout.py "$W" "$W/ui.trace" --ticks); rm -rf "$W/lay"; mkdir -p "$W/lay"
T $B32 - /dev/null --state-only --romdisk "$RU" --route tests/dialog/ui.route --max-ticks 3000 --dump "$W/lay" --dump-frames "$TK" > /dev/null 2>&1
if out=$(python3 tests/dialog/check_layout.py "$W/lay" "$W/ui.trace"); then ok "$out"; else bad "dialog layout (F4)" "$out"; fi

echo "== order variations (GDD 17; plan 4): the story routes, with the bot playing the battles"
python3 tools/dialog/lint.py --dump > "$W/village.dump"
for r in $ROUTES; do
  TMO=300 T $B32 - "$W/$r.32" --state-only --route tests/dialog/routes/$r.route --max-ticks 90000 > /dev/null 2> "$W/$r.err"
  TMO=300 T $B64 - "$W/$r.64" --state-only --route tests/dialog/routes/$r.route --max-ticks 90000 > /dev/null 2>&1
  res=$(grep "^E route" "$W/$r.32")
  ui=$(python3 tests/dialog/check_dialog.py "$W/$r.32" "$W/village.dump" 2>&1 | head -4)
  if echo "$res" | grep -q "done: [0-9]* expectations, 0 failures" && echo "$ui" | grep -q "OK$" && cmp -s "$W/$r.32" "$W/$r.64"; then
    ok "$r: $(echo "$res" | sed 's/^E route //'); dialog UI rules hold ($(echo "$ui" | grep -o '^dialog UI: [0-9]* dialogs')); float == double ($(grep -c '^F [0-9]' "$W/$r.32") ticks)"
  else bad "$r" "$res | $ui | $(cmp "$W/$r.32" "$W/$r.64" 2>&1 | head -1) | $(head -c 300 "$W/$r.err")"; fi
done
# routes 1, 2 and 4 end in the same quest state (plan 11)
final() { grep "^Q" "$W/$1.32" | tail -1 | grep -o "stage=[0-9]* .*flags=[^ ]*" | sed 's/ potions=[0-9]*//; s/ hp=[0-9]*//'; }
if [ "$(final r1_canonical)" = "$(final r2_patrols_first)" ] && [ "$(final r1_canonical)" = "$(final r4_everyone_twice)" ]; then
  ok "routes 1, 2 and 4 end in the same quest state: $(final r1_canonical)"; else bad "final states differ" "$(final r1_canonical) / $(final r2_patrols_first) / $(final r4_everyone_twice)"; fi

echo "== checkpoints: a lost battle restores the last checkpoint byte for byte (S0, S1, Brom's rest; the barrow's C3: tests/barrow)"
for r in cp_s0 r6_lose_retry cp_s3; do
  if out=$(python3 tests/dialog/check_checkpoints.py "$W/$r.32"); then ok "$r: $out"; else bad "$r checkpoints" "$out"; fi
done

echo "== rules: the learned Fireball (GDD 11.1), potions and max HP from the quest"
python3 tests/dialog/check_firelock.py --write-input "$W/fl.in"
for k in 0 1; do T $B32 "$W/fl.in" "$W/fl$k.trace" --battle-test test_slimes --state-only --view --skills $k > /dev/null; done
if out=$(python3 tests/dialog/check_firelock.py "$W/fl0.trace" "$W/fl1.trace"); then ok "$out"; else bad "fire lock UI" "$out"; fi
for k in 0 1; do T $B32 - /dev/null --battle-test test_slimes --state-only --bot cycle:F --skills $k --turn-log "$W/tl$k.log" > /dev/null; done
a0=$(grep '^ACT' "$W/tl0.log" | awk '$3=="hero"{print $4}' | sort -u | tr '\n' ' '); a1=$(grep '^ACT' "$W/tl1.log" | awk '$3=="hero"{print $4}' | sort -u | tr '\n' ' ')
[ "$a0" = "attack " ] && [ "$a1" = "fireball " ] && ok "the bot's Fire falls back to Attack while Fire is locked (hero: $a0); learned, it casts (hero: $a1)" || bad "fire lock turn logs" "locked: $a0 learned: $a1"
TMO=300 T $B32 - /dev/null --state-only --route tests/dialog/routes/r1_canonical.route --max-ticks 90000 --turn-log "$W/r1.log" > /dev/null 2>&1
hp_patrol=$(grep "^ST" "$W/r1.log" | grep -v "warden:" | grep -o "hero:[0-9]*/[0-9]*" | cut -d/ -f2 | sort -u | tr '\n' ' ')
# slice 3D-6: the Warden is the barrow's (tests/barrow/run.sh checks his battle's 30 HP)
[ "$hp_patrol" = "20 " ] && ok "field battles use the quest's max HP: 20 at the patrols (the Warden's 30: tests/barrow)" || bad "max HP in battles" "patrols: $hp_patrol"
if [ "$Q" != 1 ]; then
  eq_fail=""
  for f in tests/battle3d/baseline/*.log; do
    b=$(basename "$f" .log); enc=${b%%__*}; pol=$(echo "${b#*__}" | sed 's/_/:/')
    for v in f64 f32; do T ./build/battle_trace_$v - /dev/null --battle-test "$enc" --state-only --bot "$pol" --rules legacy --turn-log "$W/eq_$v.log"; done
    cmp -s "$f" "$W/eq_f64.log" && cmp -s "$f" "$W/eq_f32.log" || eq_fail="$eq_fail $b"
  done
  [ -z "$eq_fail" ] && ok "legacy rules unchanged: the 60 slice 3D-4 baselines replay turn for turn (float and double)" || bad "legacy baselines differ:$eq_fail"
fi

echo "== budgets (plan 9): resources, vertices, heap, SH4 estimate"
printf '0.0166667 00\n' > "$W/one.in"
T $B32 "$W/one.in" "$W/res.trace" --state-only --resources > /dev/null
res=$(grep "^D resources" "$W/res.trace" | sed 's/^D resources: //')
if python3 - "$res" <<'PYEOF'
import re, sys
s = sys.argv[1]
v = lambda k: int(re.search(k, s).group(1))
# budgets of the VS1 art reconciliation (docs/SLICE_VS1_ART.md, Budgets): 28 of 32 texture slots, 1 MiB of textures
# (of ~4.4 MiB free), 160 materials, 1,600 KiB of the 1,792 KiB model arena
# slice 3D-6 (docs/NEXT_SLICE_BARROW.md 9): 30 slots, 1 MiB, 200 materials, 1,700 KiB of arena, 32 KiB of script pools
ok = v(r'textures (\d+)/') <= 30 and v(r'VRAM (\d+) KiB') <= 1280 and v(r'materials (\d+)/') <= 200 and \
     v(r'model arena (\d+) KiB') <= 1700 and v(r'animation library (\d+) KiB') <= 384 and v(r'script pools (\d+) B') <= 32 * 1024
sys.exit(0 if ok else 1)
PYEOF
then ok "resources within budget: $res"; else bad "resources over budget" "$res"; fi
rd_bytes=$(find romdisk -type f -exec cat {} + | wc -c)
[ "$rd_bytes" -le 5500000 ] && ok "romdisk $rd_bytes bytes (<= 5.5 MB; slice 3D-7: + the audio, docs/NEXT_SLICE_SOUND_SAVE.md 8; tools/audio/lint_cues.py holds the audio to 2.4 MiB)" || bad "romdisk $rd_bytes bytes"
TMO=300 T $B32 - "$W/r1v.trace" --state-only --view --route tests/dialog/routes/r1_canonical.route --max-ticks 90000 > /dev/null 2>&1
for f in field_walk field_fuzz2; do T $B32 tests/inputs/$f.in "$W/v_$f.trace" --state-only --view > /dev/null 2>&1; done
vstat=$(cat "$W/r1v.trace" "$W/v_field_walk.trace" "$W/v_field_fuzz2.trace" | awk '/^V /{for(i=2;i<=NF;i++){split($i,a,"=");v[a[1]]=a[2]} m=v["mode"]; if(v["out"]>o[m])o[m]=v["out"]; if(v["arena"]>ar[m])ar[m]=v["arena"]; d+=v["drops"]} END{printf "field out %d arena %d, dialog out %d arena %d, drops %d", o["field"], ar["field"], o["dialog"], ar["dialog"], d+0}')
if echo "$vstat" | python3 -c "
import re,sys; s=sys.stdin.read(); n=list(map(int,re.findall(r'\d+',s)))
sys.exit(0 if n[0]<=18000 and n[2]<=18000 and n[4]==0 and n[1]<=18000 and n[3]<=18000 else 1)"; then
  ok "vertices per frame: $vstat (<= 18,000 submitted and written; arena of 20,480; slice 3D-6: cutscene cameras may see more than the field's)"; else bad "vertex budget" "$vstat"; fi
g++ -std=gnu++17 -O2 -DHOST_ALLOC_COUNT $SHZ src/*.cpp platform/host/host_engine.cpp platform/host/trace_main.cpp platform/host/battle_bot.cpp \
    platform/host/story.cpp build/sh4zam-host/libsh4zam.a -o build/battle_trace_alloc 2> "$W/alloc_build.err" || bad "alloc build" "$(cat "$W/alloc_build.err")"
heap=""; for r in r1_canonical r4_everyone_twice cp_s3; do
  TMO=300 T ./build/battle_trace_alloc - "$W/alloc.trace" --state-only --route tests/dialog/routes/$r.route --max-ticks 90000 > /dev/null 2>&1
  heap="$heap $(grep '^D heap' "$W/alloc.trace" | grep -o '[0-9]* allocations')"; done
[ "$(echo "$heap" | tr -s ' ' | sed 's/^ //')" = "0 allocations 0 allocations 0 allocations" ] && ok "no heap allocation inside any tick or draw after the boot (routes 1, 4 and the checkpoint route: battles, dialogs, portraits, retries)" || bad "heap allocations during play:$heap"
est=$(python3 tests/dialog/sh4_model.py "$W/v_field_walk.trace" "$W/v_field_fuzz2.trace" "$W/r1v.trace" 2>&1)
echo "$est" | sed 's/^/       /'
fw=$(echo "$est" | grep -v "^calibration" | grep " field " | grep -o "worst [0-9.]* M" | grep -o "[0-9.]*" | sort -n | tail -1); dw=$(echo "$est" | grep " dialog " | grep -o "worst [0-9.]* M" | grep -o "[0-9.]*")
if python3 -c "import sys; f,d=float('$fw'),float('$dw'); sys.exit(0 if f<=1.60 and d<=1.60 else 1)"; then
  ok "SH4 estimate (calibrated host model, not a measurement): field worst $fw M, dialog worst $dw M (<= 1.60 M)"; else bad "SH4 estimate over budget" "$est"; fi
if [ "${SH4:-0}" = 1 ]; then
  if out=$(TMO=2400 T bash tests/dialog/sh4_budget.sh 2>&1); then echo "$out" | sed 's/^/       /'; ok "SH4 (qemu) frame cost within budget"; else bad "SH4 (qemu) frame cost" "$out"; fi
fi

if [ "$Q" != 1 ]; then
  echo "== sanitizers (ASan + UBSan) over the story routes"
  g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $SHZ src/*.cpp platform/host/host_engine.cpp \
      platform/host/trace_main.cpp platform/host/battle_bot.cpp platform/host/story.cpp build/sh4zam-host/libsh4zam.a -o "$W/trace_asan" 2> "$W/asan_build.err" || bad "asan build" "$(head -5 "$W/asan_build.err")"
  asan_bad=""
  for r in r1_canonical r2_patrols_first r3_skip_brom r4_everyone_twice r5_loops r6_lose_retry; do
    ASAN_OPTIONS=detect_leaks=0 TMO=600 T "$W/trace_asan" - /dev/null --state-only --route tests/dialog/routes/$r.route --max-ticks 90000 > "$W/asan.out" 2>&1 || asan_bad="$asan_bad $r(exit)"
    grep -q "runtime error\|ERROR: AddressSanitizer" "$W/asan.out" && asan_bad="$asan_bad $r($(grep -m1 'runtime error\|AddressSanitizer' "$W/asan.out" | cut -c1-120))"
  done
  ASAN_OPTIONS=detect_leaks=0 T "$W/trace_asan" - /dev/null --state-only --romdisk "$RU" --route tests/dialog/ui.route --max-ticks 3000 > "$W/asan.out" 2>&1 || asan_bad="$asan_bad ui"
  bash -c "BIN='$W/trace_asan' WORK='$W/asan_lint' ASAN_OPTIONS=detect_leaks=0 bash tests/dialog/check_lint_fixtures.sh" > "$W/asan_lint.out" 2>&1 || asan_bad="$asan_bad lint_fixtures"
  [ -z "$asan_bad" ] && ok "ASan/UBSan clean: routes 1-6, the UI route, every lint fixture's load" || bad "sanitizers:$asan_bad" "$(tail -5 "$W/asan_lint.out")"
fi

echo "== assets reproducible"
snap="$W/assets"; rm -rf "$snap"; mkdir -p "$snap"
cp romdisk/fonts/dialog.fnt romdisk/fonts/dialog.tex tools/dialog/reports/dialog_font.json "$snap/"
python3 tools/dialog/make_font.py > /dev/null
same=1; for f in dialog.fnt dialog.tex; do cmp -s "$snap/$f" romdisk/fonts/$f || same=0; done
cmp -s "$snap/dialog_font.json" tools/dialog/reports/dialog_font.json || same=0
git checkout -- docs/dialog/font_preview.png 2>/dev/null
[ $same = 1 ] && ok "make_font.py reproduces the dialog font byte for byte (fnt, atlas, report)" || bad "the dialog font differs from the generator"
if [ -n "${BPY:-}" ]; then
  snap2="$W/bpy_assets"; rm -rf "$snap2"; mkdir -p "$snap2"
  cp romdisk/anims/rig_medium.dma assets/vs1_art/bodies/wren.dmf assets/vs1_art/bodies/brom.dmf romdisk/portraits/*.dtx "$snap2/"
  BPY="$BPY" bash tools/battle3d/build_assets.sh > /dev/null 2>&1
  BPY="$BPY" bash tools/dialog/build_assets.sh > /dev/null 2>&1
  same=1; n=0; for f in "$snap2"/*; do g=$(ls romdisk/anims/$(basename "$f") assets/vs1_art/bodies/$(basename "$f") romdisk/portraits/$(basename "$f") 2>/dev/null | head -1); n=$((n+1)); cmp -s "$f" "$g" || same=0; done
  git checkout -- tools/battle3d/reports tools/dialog/reports docs/dialog/portraits.png docs/dialog/font_preview.png docs/battle3d/fx_atlas.png 2>/dev/null
  [ $same = 1 ] && ok "Blender-built assets rebuild byte for byte: the library (with walk), the bodies of Wren and Brom, 9 portraits ($n files)" || bad "Blender-built assets differ"
fi

echo "RESULT: pass=$pass fail=$fail"
[ $fail -eq 0 ] || { printf '  failed: %s\n' "${failed[@]}"; exit 1; }
