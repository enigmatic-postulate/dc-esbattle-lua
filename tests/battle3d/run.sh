#!/usr/bin/env bash
# Slice 3D-4 (3D battle + the Bone Warden) test suite. Runs from any directory; needs g++, python3
# (numpy, pillow) and an SH4ZAM checkout (as `make host`).
#   tests/battle3d/run.sh            everything below (about 6 minutes)
#   QUICK=1 tests/battle3d/run.sh    fewer seeds / frames (about 2 minutes)
#   BPY=<bpy python> ...             also rebuilds the Blender-made assets and checks they are byte-identical
#   SH4=1 ...                        also runs the SH4 build under qemu: instruction counts per frame (see
#                                    tests/sh4zam/README.md for the toolchain; SH_ELF, QEMU_SH4, WORK)
# The older suite (tests/run_tests.sh) still covers the field; see docs/SLICE_3D_BATTLE.md for which of
# its checks were already failing before this slice.
set -u
cd "$(dirname "$0")/../.."
W="${WORK:-/tmp/b3_tests}"; mkdir -p "$W"
Q="${QUICK:-0}"
pass=0; fail=0; failed=()
ok()  { pass=$((pass+1)); echo "  PASS $1"; }
bad() { fail=$((fail+1)); failed+=("$1"); echo "  FAIL $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/       /' | head -12; }
T() { timeout "${TMO:-120}" "$@"; }

echo "== build (host, float + double) and warnings"
make -s host >/dev/null 2>&1 || { echo "host build failed"; exit 1; }
SHZ=$(make -s print-host-shz-flags)
warn=$(g++ -std=gnu++17 -Wall -Wextra -Wshadow $SHZ -fsyntax-only src/*.cpp 2>&1 | grep -c warning)
[ "$warn" = "0" ] && ok "src builds with 0 warnings (-Wall -Wextra -Wshadow)" || bad "src warnings: $warn"
g++ -std=gnu++17 -O2 -Wall -Wextra $SHZ src/anim.cpp src/dmf.cpp src/audio.cpp tests/battle3d/anim_dump.cpp platform/host/host_engine.cpp \
    build/sh4zam-host/libsh4zam.a -o build/anim_dump 2>"$W/anim_dump.err" || bad "anim_dump build" "$(cat "$W/anim_dump.err")"

echo "== animation library: C++ anim::sample == tools/battle3d/dmfio.py (every clip, 6 times, 4 models)"
for m in knight skel_warrior skel_rogue skel_mage; do
  ./build/anim_dump romdisk "$m" > "$W/ad_$m.txt" 2>/dev/null
  if out=$(python3 tests/battle3d/anim_check.py "$W/ad_$m.txt" romdisk "$m" 1e-4); then ok "$out"; else bad "anim crosscheck $m" "$out"; fi
done

echo "== rules unchanged: turn-level equivalence with the sprite battle (legacy rules, 60 baselines, f64 + f32)"
eq_pass=0; eq_fail=0; eq_bad=""
for f in tests/battle3d/baseline/*.log; do
  b=$(basename "$f" .log); enc=${b%%__*}; pol=$(echo "${b#*__}" | sed 's/_/:/')
  for v in f64 f32; do
    T ./build/battle_trace_$v - /dev/null --battle-test "$enc" --state-only --bot "$pol" --rules legacy --turn-log "$W/eq_$v.log"
  done
  if cmp -s "$f" "$W/eq_f64.log" && cmp -s "$f" "$W/eq_f32.log"; then eq_pass=$((eq_pass+1)); else eq_fail=$((eq_fail+1)); eq_bad="$eq_bad $b"; fi
done
[ $eq_fail = 0 ] && ok "3D build == sprite build, every turn of $eq_pass bot battles (float and double)" || bad "equivalence: $eq_fail differ:$eq_bad"

echo "== the slice's rules (independent Python model of GDD 11.1 / 12: damage, guard, burn, potion, boss AI)"
rm -rf "$W/rl"; mkdir -p "$W/rl"
SEEDS=$([ "$Q" = 1 ] && echo 10 || echo 40)
for mode in good naive noguard; do for s in $(seq 1 "$SEEDS"); do
  T ./build/battle_trace_f64 - /dev/null --battle-test warden --state-only --view2d --bot "boss:$mode:$s" --turn-log "$W/rl/w_${mode}_$s.log"
done; done
for enc in test_slimes slimes_2 guard_duel; do for pol in cycle:GA cycle:FGGA guard random:3 random:8; do
  T ./build/battle_trace_f64 - /dev/null --battle-test "$enc" --state-only --view2d --bot "$pol" --turn-log "$W/rl/${enc}_${pol/:/_}.log"
done; done
if out=$(python3 tests/battle3d/check_rules.py "$W"/rl/*.log); then ok "$out"; else bad "rules model" "$out"; fi
n_skills=$(grep -h "^ACT" "$W"/rl/w_*.log | awk '{print $4}' | sort -u | tr '\n' ' ')
case "$n_skills" in *bone_slash*bone_ward*charge*fireball*grave_fire*guard*phase*potion*raise_dead*soul_rend*) ok "every boss skill exercised ($n_skills)";;
  *) bad "boss skills not all exercised: $n_skills";; esac
# the 3D build plays the same boss fight as the sprite build (the boss rules, not just the legacy ones)
for s in 1 2 3; do
  T ./build/battle_trace_f32 - /dev/null --battle-test warden --state-only --bot "boss:good:$s" --turn-log "$W/b3d_$s.log"
  T ./build/battle_trace_f64 - /dev/null --battle-test warden --state-only --view2d --bot "boss:good:$s" --turn-log "$W/b2d_$s.log"
done
if cmp -s "$W/b3d_1.log" "$W/b2d_1.log" && cmp -s "$W/b3d_2.log" "$W/b2d_2.log" && cmp -s "$W/b3d_3.log" "$W/b2d_3.log"; then
  ok "boss fight: 3D build == sprite build, turn by turn (3 seeds)"; else bad "boss fight differs between the 3D and the sprite build"; fi

echo "== boss balance (GDD 12.6 targets)"
BSEEDS=$([ "$Q" = 1 ] && echo 30 || echo 100)
if out=$(TMO=400 T bash tests/battle3d/boss_balance.sh "$BSEEDS"); then
  echo "$out" | sed 's/^/       /'
  if python3 - "$out" <<'PYEOF'
import re, sys
r = {}
for ln in sys.argv[1].splitlines():
    m = re.match(r'(\w+)\s+won\s+(\d+)/(\d+).*mean HP at a win (\S+)\s+mean hero turns (\d+)\s+losses to Soul Rend (\d+)', ln)
    if m: r[m.group(1)] = dict(win=100.0 * int(m.group(2)) / int(m.group(3)), n=int(m.group(3)), wins=int(m.group(2)),
                              hp=float(m.group(4)) if m.group(4) != '-' else 0, turns=int(m.group(5)), rend=int(m.group(6)))
g, n, q = r['good'], r['naive'], r['noguard']
slack = 0 if g['n'] >= 100 else 7        # fewer seeds: wider margins
okk = (g['win'] >= 95 - slack and 8 <= g['hp'] <= 20 and 30 - slack <= n['win'] <= 60 + slack and q['win'] <= g['win'] - 10
       and q['rend'] * 2 > (q['n'] - q['wins']) and 12 <= g['turns'] <= 16)
sys.exit(0 if okk else 1)
PYEOF
  then ok "boss balance: good >= 95 % ending with 8-20 HP in 12-16 turns, naive 30-60 %, no-guard worse and losing to Soul Rend"
  else bad "boss balance off target"; fi
else bad "boss balance run" "$out"; fi

echo "== 3D battle UI (B back, entry lock, Fire confirm, cursor = screen order, ring wraps, A skips the intro)"
python3 tests/battle3d/check_ui3d.py --write-input "$W/ui3d.in"
cmp -s "$W/ui3d.in" tests/battle3d/ui3d.in || bad "tests/battle3d/ui3d.in is stale (regenerate with --write-input)"
T ./build/battle_trace_f32 tests/battle3d/ui3d.in "$W/ui.trace" --battle-test test_slimes --state-only --view > /dev/null
if out=$(python3 tests/battle3d/check_ui3d.py "$W/ui.trace"); then ok "$out"; else bad "UI 3D" "$out"; fi

for h in "" "--bot-hold-a"; do
  T ./build/battle_trace_f32 - "$W/hold.trace" --battle-test test_slimes --state-only --bot cycle:AFG --turn-log "$W/hold$h.log" $h > /dev/null
  grep -c "^F [0-9]" "$W/hold.trace" > "$W/hold$h.n"
done
if cmp -s "$W/hold.log" "$W/hold--bot-hold-a.log" && [ "$(cat "$W/hold--bot-hold-a.n")" -lt $(( $(cat "$W/hold.n") * 3 / 4 )) ]; then
  ok "holding A: the same battle turn for turn in $(cat "$W/hold--bot-hold-a.n") ticks instead of $(cat "$W/hold.n")"
else bad "hold A (2x)" "ticks $(cat "$W/hold.n") -> $(cat "$W/hold--bot-hold-a.n")"; fi

echo "== game flow: field -> 3D battle -> field, the gate, the barrow, the boss fight, the ending; a loss shows the death screen"
python3 -c "print('\n'.join(['0.0166667 00'] * 40000))" > "$W/idle.in"
# slice 3D-5: the story driver (platform/host/story.cpp) walks the route; the bot plays the three battles. Slice 3D-6: the
# Warden is the barrow's; his win runs the ending (T10), the credits, the results, and the title comes back with its crest
TMO=300 T ./build/battle_trace_f32 - "$W/flow.trace" --state-only --story --route tests/battle3d/flow.route --bot-battles "boss:good:1" --max-ticks 40000 > /dev/null 2>&1
res=$(grep "^E route" "$W/flow.trace")
battles=$(grep -c " battle$" <(grep "^N " "$W/flow.trace"))
if echo "$res" | grep -q "done: [0-9]* expectations, 0 failures" && [ "$battles" = 3 ]; then
  ok "three battles won from the field (both patrols, then the Warden in the barrow); the ending ran to the title ($res)"
else bad "game flow" "$res battles=$battles"; fi
grep "^N " "$W/flow.trace" | awk '{print $3}' | tr '\n' ' ' > "$W/flow_modes.txt"
# after a won battle the field returns, or the after_battle script's dialog over it (slice 3D-5); the boss's win hands over
# to the ending (to_field = T10 over the battle, then the ending's dialog) - slice 3D-6
[ "$(grep -oE "to_battle battle to_field (field|dialog)" "$W/flow_modes.txt" | wc -l)" = 3 ] && ! grep -q "death" "$W/flow_modes.txt" && \
  ok "transitions field -> to_battle -> battle -> to_field -> field (or a dialog), three times" || bad "mode sequence" "$(cat "$W/flow_modes.txt")"
# a lost battle: the death screen (slice 3D-6; it replaced the game-over screen), then A restores every entity
TMO=300 T ./build/battle_trace_f32 "$W/idle.in" "$W/lose.trace" --state-only --bot-battles "guard" --rules legacy \
  --walk-to "20.5,15.6;27.5,15.6" --max-ticks 6000 > /dev/null
if grep -q "mode=death" "$W/lose.trace"; then ok "a lost battle ends in the death screen"; else bad "no death screen after a lost battle"; fi

echo "== frame budget and determinism of the 3D battles (<= 18,000 submitted vertices: docs/SLICE_VS1_ART.md, Budgets)"
budget_fail=""
for run in "test_slimes cycle:AFG" "test_slimes fire" "slimes_2 random:3" "guard_duel cycle:FGGA" "warden boss:good:2" "warden boss:naive:5"; do
  set -- $run
  T ./build/battle_trace_f32 - "$W/b.trace" --battle-test "$1" --state-only --bot "$2" --bot-wait 120 --bot-linger 120 --view > /dev/null
  T ./build/battle_trace_f64 - "$W/b64.trace" --battle-test "$1" --state-only --bot "$2" --bot-wait 120 --bot-linger 120 --view > /dev/null
  cmp -s <(grep -v "^D" "$W/b.trace") <(grep -v "^D" "$W/b64.trace") || budget_fail="$budget_fail float!=double($run)"
  r=$(awk '/^B /{for(i=2;i<=NF;i++){split($i,a,"="); v[a[1]]=a[2]} n++; if(v["out"]>m)m=v["out"]; if(v["arena"]>ma)ma=v["arena"]; if(v["parts"]>mp)mp=v["parts"]; if(v["drops"]>md)md=v["drops"]} END{print m, ma, mp, md+0, n}' "$W/b.trace")
  set -- $r
  echo "       $run: worst frame $1 vertices, arena peak $2, particles peak $3, dropped batches $4 ($5 frames)"
  [ "$1" -le 18000 ] || budget_fail="$budget_fail over-budget($run:$1)"
  [ "$4" = 0 ] || budget_fail="$budget_fail drops($run)"
done
[ -z "$budget_fail" ] && ok "every 3D battle frame within budget, no dropped batches, float == double (tick-exact)" || bad "budget/determinism:$budget_fail"

echo "== view-culled arena props draw the same pixels (battles not used to build them)"
RD="$W/rd_orig"; rm -rf "$RD"; cp -r romdisk "$RD"
# the arena houses' source models (pre-VS1 field houses, assets/arena_src/; the field's house_*_r0 are VS1's now)
cp assets/arena_src/house_plaster_r0.dmf assets/arena_src/house_stone_r0.dmf "$RD/models/"
sed -i 's/^prop house_plaster_b /prop house_plaster_r0/; s/^prop house_stone_b  /prop house_stone_r0/' "$RD/arenas/village.txt"
EVERY=$([ "$Q" = 1 ] && echo 97 || echo 29)
for run in "test_slimes random:11" "slimes_2 cycle:GFA" "guard_duel random:12"; do
  set -- $run
  d1="$W/vc_a_$1"; d2="$W/vc_b_$1"; rm -rf "$d1" "$d2"; mkdir -p "$d1" "$d2"
  T ./build/battle_trace_f32 - /dev/null --battle-test "$1" --state-only --bot "$2" --bot-wait 120 --bot-linger 200 --dump "$d1" --dump-every $EVERY --romdisk "$RD" > /dev/null
  T ./build/battle_trace_f32 - /dev/null --battle-test "$1" --state-only --bot "$2" --bot-wait 120 --bot-linger 200 --dump "$d2" --dump-every $EVERY > /dev/null
done
if out=$(python3 tests/battle3d/compare_dumps.py "$W" "$RD" romdisk); then ok "$out"; else bad "view culling" "$out"; fi

echo "== sanitizers (ASan + UBSan) over full battles"
g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $SHZ src/*.cpp platform/host/host_engine.cpp \
    platform/host/trace_main.cpp platform/host/battle_bot.cpp platform/host/story.cpp build/sh4zam-host/libsh4zam.a -o "$W/trace_asan" 2>"$W/asan_build.err" || bad "asan build" "$(cat "$W/asan_build.err")"
asan_bad=""
for run in "test_slimes cycle:AFG" "warden boss:good:4" "guard_duel random:2"; do
  set -- $run
  ASAN_OPTIONS=detect_leaks=0 T "$W/trace_asan" - /dev/null --battle-test "$1" --state-only --bot "$2" --bot-wait 100 > "$W/asan.out" 2>&1 || asan_bad="$asan_bad $1"
  grep -q "runtime error\|ERROR: AddressSanitizer" "$W/asan.out" && asan_bad="$asan_bad $1($(grep -m1 'runtime error\|AddressSanitizer' "$W/asan.out" | cut -c1-120))"
done
ASAN_OPTIONS=detect_leaks=0 T "$W/trace_asan" "$W/idle.in" /dev/null --state-only --bot-battles cycle:AFG --walk-to "20.5,15.6;27.5,15.6" --max-ticks 4000 > "$W/asan.out" 2>&1 || asan_bad="$asan_bad flow"
[ -z "$asan_bad" ] && ok "ASan/UBSan clean: village and boss battles, field flow" || bad "sanitizers:$asan_bad"

echo "== assets reproducible"
snap="$W/assets"; rm -rf "$snap"; mkdir -p "$snap"
for f in romdisk/tex/fx.tex romdisk/tex/dirt.png romdisk/models/tomb_round.dmf romdisk/models/tomb_cross.dmf romdisk/models/crypt.dmf; do cp "$f" "$snap/"; done
python3 tools/battle3d/make_fx.py > /dev/null && python3 tools/battle3d/make_props.py > /dev/null
same=1; for f in fx.tex dirt.png tomb_round.dmf tomb_cross.dmf crypt.dmf; do
  g=$(ls romdisk/tex/$f romdisk/models/$f 2>/dev/null | head -1); cmp -s "$snap/$f" "$g" || { same=0; cp "$snap/$f" "$g"; }; done
git checkout -- docs/battle3d/fx_atlas.png 2>/dev/null
[ $same = 1 ] && ok "make_fx.py and make_props.py reproduce the committed files byte for byte" || bad "procedural assets differ from the generators"
if [ -n "${BPY:-}" ]; then
  snap2="$W/bpy_assets"; rm -rf "$snap2"; mkdir -p "$snap2"
  cp romdisk/anims/rig_medium.dma assets/vs1_art/bodies/skel_rogue.dmf romdisk/models/w_*.dmf "$snap2/"
  BPY="$BPY" bash tools/battle3d/build_assets.sh > /dev/null 2>&1
  same=1; for f in "$snap2"/*; do g=$(ls romdisk/anims/$(basename "$f") assets/vs1_art/bodies/$(basename "$f") romdisk/models/$(basename "$f") 2>/dev/null | head -1); cmp -s "$f" "$g" || same=0; done
  [ $same = 1 ] && ok "Blender-built assets (animation library, the Rogue's body, weapons) rebuild byte for byte" || bad "Blender-built assets differ"
  git checkout -- tools/battle3d/reports 2>/dev/null
  if "$BPY" tools/battle3d/build_anims.py --check 2>/dev/null | grep -q "OK"; then ok "clip convention check against the shipped field clips"; else bad "clip convention check"; fi
fi

if [ "${SH4:-0}" = 1 ]; then
  echo "== SH4 build under qemu: instructions per frame (tests/sh4zam/sh4kit.sh)"
  if out=$(TMO=1800 T bash tests/battle3d/sh4_budget.sh); then echo "$out" | sed 's/^/       /'; ok "SH4 frame cost within budget"; else bad "SH4 frame cost" "$out"; fi
fi

echo "RESULT: pass=$pass fail=$fail"
[ $fail -eq 0 ] || { printf '  failed: %s\n' "${failed[@]}"; exit 1; }
