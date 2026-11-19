#!/usr/bin/env bash
# Slice 3D-6 ("Into the Barrow": the game flow, the gate, the barrow, the boss's staging, the ending, the frame around the
# game) test suite - docs/NEXT_SLICE_BARROW.md 11. Runs from any directory; needs g++, python3 (numpy, pillow) and an
# SH4ZAM checkout (as `make host`). About 6 minutes.
#   tests/barrow/run.sh            everything below
#   QUICK=1 tests/barrow/run.sh    skips the sanitizer build and the float == double reruns
# The other suites (tests/dialog, tests/vs1art, tests/battle3d, tests/run_tests.sh) keep running separately.
set -u
cd "$(dirname "$0")/../.."
W="${WORK:-/tmp/barrow_tests}"; rm -rf "$W"; mkdir -p "$W"
Q="${QUICK:-0}"
pass=0; fail=0; failed=()
ok()  { pass=$((pass+1)); echo "  PASS $1"; }
bad() { fail=$((fail+1)); failed+=("$1"); echo "  FAIL $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/       /' | head -14; }
T() { timeout "${TMO:-300}" "$@"; }
B32=./build/battle_trace_f32; B64=./build/battle_trace_f64
R=tests/barrow/routes
run() {   # NAME [extra args...]: a --boot route (the Dreamcast's start: the title)
  local n="$1"; shift
  T $B32 - "$W/$n.32" --state-only --boot --route "$R/$n.route" --max-ticks 90000 "$@" > /dev/null 2> "$W/$n.err"
}
route_ok() { grep -q "^E route done: [0-9]* expectations, 0 failures" "$1"; }
# the quest's final state (Brom's optional rest and refill aside: a route may take them or not)
final_q() { grep "^Q" "$1" | tail -1 | grep -o "stage=[0-9]* .*flags=[^ ]*" | sed 's/ potions=[0-9]*//; s/ hp=[0-9]*//; s/,brom_refill//; s/,talked_brom//'; }

echo "== build (host, float + double), warnings, the KOS backend"
make -s host >/dev/null 2>&1 || { echo "host build failed"; exit 1; }
SHZ=$(make -s print-host-shz-flags)
warn=$(g++ -std=gnu++17 -Wall -Wextra -Wshadow $SHZ -fsyntax-only src/*.cpp platform/host/*.cpp 2>&1 | grep -c "warning\|error")
[ "$warn" = 0 ] && ok "src and the host harness build with 0 warnings (-Wall -Wextra -Wshadow)" || bad "warnings: $warn"
kw=0; for d in "" "-DBATTLE_DIAG=1" "-DNO_FOG=1" "-DINPUT_OVERLAY=1 -DR3D_SAFE=1" "-DPVR_DEFAULTS=1 -DGROUND_NO_VQ=1" "-DNO_FOG=1 -DGROUND_NO_VQ=1"; do
  c=$(g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wshadow -Itests/dialog/kos_stub $SHZ $d platform/kos/main.cpp 2>&1 | grep -c "warning\|error"); kw=$((kw+c)); done
[ "$kw" = 0 ] && ok "the KOS backend compiles against stub KOS headers in 6 flavours (incl. NO_FOG=1, GROUND_NO_VQ=1)" || bad "KOS backend stub check: $kw warnings/errors"
grep -q "src/lights.o src/screens.o src/transitions.o" makefile && ok "the makefile builds every new module (lights, screens, transitions)" || bad "makefile: a new module is missing"

echo "== scripts (lint, parity with the game's compiler) and data"
if out=$(python3 tools/dialog/lint.py); then ok "$out"; else bad "lint" "$out"; fi
python3 tools/dialog/lint.py --dump > "$W/py.dump"; T $B32 - /dev/null --dump-scripts > "$W/cpp.dump"
cmp -s "$W/py.dump" "$W/cpp.dump" && ok "the game's compiler == lint.py across 3 files and 2 maps ($(grep -c '^[0-9]' "$W/cpp.dump") ops, $(grep '^actors' "$W/cpp.dump"))" \
  || bad "compiled scripts differ" "$(diff "$W/py.dump" "$W/cpp.dump" | head -6)"
grep -q "slice 3D-6" romdisk/scripts/barrow.txt && grep -q "slice 3D-6" romdisk/scripts/ending.txt && grep -c "# slice 3D-6" romdisk/scripts/*.txt > /dev/null && \
  ok "every GDD deviation in the scripts is marked '# slice 3D-6' ($(cat romdisk/scripts/*.txt | grep -c '# slice 3D-6') marks)" || bad "deviation marks"

echo "== T1 the full playthrough: title -> T2 -> intro -> quest -> Corwin -> barrow -> waystone -> T8 -> boss -> ending -> title"
run full --turn-log "$W/full.log"
if route_ok "$W/full.32"; then ok "full.route: $(grep '^E route' "$W/full.32" | sed 's/^E route //') ($(grep -c '^F [0-9]' "$W/full.32") ticks)"; else bad "full.route" "$(grep '^E route\|FAIL' "$W/full.32" | head -5; head -c 300 "$W/full.err")"; fi
if out=$(python3 tests/barrow/check_flow.py "$W/full.32" --expect-modes title,card,dialog,field,to_battle,battle,map_change,field,to_battle,battle,to_field,dialog,map_change,dialog,credits,dialog,results,dialog,title); then
  ok "every mode change is a catalogued transition with its ticks: $out"; else bad "flow / transitions" "$out"; fi
if out=$(python3 tests/barrow/check_results.py "$W/full.32" "$W/full.log"); then ok "$out"; else bad "results card" "$out"; fi
au=$(grep "^A " "$W/full.32" | awk '{print $2}' | sort -u | tr '\n' ' ')
miss=""; for e in music_title music_village music_barrow music_battle music_boss music_dawn music_credits sting_victory sting_boss_name gate talk menu_confirm; do
  echo " $au" | grep -q " $e " || miss="$miss $e"; done
[ -z "$miss" ] && ok "audio hooks (S12) fire at their moments: $au" || bad "audio hooks missing:$miss" "$au"
if [ "$Q" != 1 ]; then
  T $B64 - "$W/full.64" --state-only --boot --route "$R/full.route" --max-ticks 90000 > /dev/null 2>&1
  cmp -s "$W/full.32" "$W/full.64" && ok "float == double over the whole playthrough (every tick's state, dialog and transition lines)" || bad "full: float != double" "$(cmp "$W/full.32" "$W/full.64")"
fi

echo "== T2 order variations: out of the barrow and back (BA3), Corwin after the gate opened"
run barrow_back
if route_ok "$W/barrow_back.32" && [ "$(final_q "$W/barrow_back.32")" = "$(final_q "$W/full.32")" ]; then
  ok "barrow_back.route ends in the full route's state: $(final_q "$W/full.32")"; else bad "barrow_back" "$(grep '^E route\|FAIL' "$W/barrow_back.32" | head -4) | $(final_q "$W/barrow_back.32")"; fi

echo "== T4 death and retry (GDD 13): the death screen, RETRY from C3 with the short boss intro, RETURN TO TITLE"
run cp_c3 --turn-log "$W/cp_c3.log"
if route_ok "$W/cp_c3.32" && out=$(python3 tests/dialog/check_checkpoints.py "$W/cp_c3.32" --expect-retries 1); then
  ok "cp_c3.route: the boss's loss restores C3 on the barrow byte for byte, full HP; $out"; else bad "cp_c3" "$(grep '^E route\|FAIL' "$W/cp_c3.32" | head -4) $out"; fi
# the retry goes from the death screen straight into the short intro (60 ticks of wait before its battle)
d=$(awk '$1=="N" && $3=="death"{d=$2} $1=="N" && $3=="to_battle" && d{print $2-d; exit}' "$W/cp_c3.32")
sh=$(awk '$1=="N" && $3=="death"{d=1} d && $1=="N" && $3=="dialog"{s=$2} d && s && $1=="N" && $3=="to_battle"{print $2-s; exit}' "$W/cp_c3.32")
first=$(awk '$1=="N" && $3=="dialog"{s=$2} $1=="N" && $3=="to_battle"{print $2-s}' "$W/full.32" | tail -1)
[ -n "$sh" ] && [ "$sh" -lt 120 ] && ok "the retry's boss intro is the short form: $sh ticks from its start to the battle (the first time's: $first)" \
  || bad "short intro" "dialog -> battle $sh ticks, death -> battle $d"
if out=$(python3 tests/barrow/check_flow.py "$W/cp_c3.32"); then ok "death flow: $out"; else bad "death flow" "$out"; fi
grep -q "^R .*retries=1 .*rank=1" "$W/cp_c3.32" && ok "one retry: the results card ranks silver ($(grep '^R ' "$W/cp_c3.32" | cut -d' ' -f3-))" || bad "rank after a retry" "$(grep '^R ' "$W/cp_c3.32")"
run rest_after_c3
if route_ok "$W/rest_after_c3.32" && out=$(python3 tests/dialog/check_checkpoints.py "$W/rest_after_c3.32" --expect-retries 1); then
  ok "rest_after_c3.route: Brom's rest after the waystone: the boss's loss restores the village (cp 9), no intro there; back in, won; $out"
else bad "rest_after_c3" "$(grep '^E route\|FAIL' "$W/rest_after_c3.32" | head -4) $out"; fi
run death_title
route_ok "$W/death_title.32" && grep -q "^T [0-9]* start T11" "$W/death_title.32" && ok "death -> RETURN TO TITLE (T11): the session is gone; a new game starts at S0" || bad "death_title" "$(grep '^E route\|FAIL' "$W/death_title.32" | head -4)"

echo "== T5 triggers and exits: the road block (once per entry, 1 tile back), the closed exit, the gate, the exits both ways"
T $B32 - "$W/road.32" --state-only --route "$R/road_block.route" --max-ticks 90000 > /dev/null 2>&1
route_ok "$W/road.32" && ok "road_block.route: $(grep '^E route' "$W/road.32" | sed 's/^E route //')" || bad "road_block" "$(grep '^E route\|FAIL' "$W/road.32" | head -5)"

echo "== T6 cutscene skips (D7): Start fast-forwards with the same state changes; never a choice"
run intro_watched; run skip_intro
first_field() { awk '$1=="N" && $3=="field"{t=$2; exit} END{print t}' "$1"; }
qw=$(grep "^Q" "$W/intro_watched.32" | tail -1 | grep -o " raw=[0-9a-f]*"); qs=$(grep "^Q" "$W/skip_intro.32" | tail -1 | grep -o " raw=[0-9a-f]*")
xw=$(grep "^X mode=field" "$W/intro_watched.32" | tail -1 | grep -o "hero=[^ ]*"); xs=$(grep "^X mode=field" "$W/skip_intro.32" | tail -1 | grep -o "hero=[^ ]*")
if route_ok "$W/intro_watched.32" && route_ok "$W/skip_intro.32" && [ "$qw" = "$qs" ] && [ "$xw" = "$xs" ] && [ "$(first_field "$W/skip_intro.32")" -lt "$(first_field "$W/intro_watched.32")" ]; then
  ok "the intro watched == skipped: Progress $qw, $xw (the field at tick $(first_field "$W/skip_intro.32") instead of $(first_field "$W/intro_watched.32"))"
else bad "skip the intro" "watched $qw $xw / skipped $qs $xs"; fi
run skip_ending
route_ok "$W/skip_ending.32" && [ "$(final_q "$W/skip_ending.32")" = "$(final_q "$W/full.32")" ] && \
  ok "the boss intro, the ending and the credits skipped: the same end state ($(grep -c '^F [0-9]' "$W/skip_ending.32") ticks instead of $(grep -c '^F [0-9]' "$W/full.32"))" \
  || bad "skip the ending" "$(grep '^E route\|FAIL' "$W/skip_ending.32" | head -4) | $(final_q "$W/skip_ending.32")"

echo "== T8 light presets and fog (host emulation in the frame dumps)"
mkdir -p "$W/fog"
T $B32 - /dev/null --state-only --boot --route "$R/full.route" --max-ticks 90000 --dump "$W/fog" --dump-frames 100,4400,5400,11380,11700 > /dev/null 2>&1
fl=""; for f in 100 4400 5400 11380 11700; do fl="$fl $f:$(grep -c '^fog ' "$W/fog/frame_$(printf %05d $f).txt" 2>/dev/null)"; done
echo "$fl" | grep -q " 100:0 4400:0 5400:1 11380:1 11700:0" && ok "fog on the barrow only (tick:fog lines $fl); the dawn's fog $(grep '^fog' "$W/fog/frame_11380.txt") lifts from the night's $(grep '^fog' "$W/fog/frame_05400.txt")" \
  || bad "fog" "$fl"
python3 tools/render_frame.py "$W/fog/frame_05400.txt" romdisk "$W/barrow.png" && ok "the host renders the fogged barrow (tools/render_frame.py: W-based table fog, MAT_MULTIPLY)" || bad "render_frame fog"

echo "== T11 the attract demo (40 s idle on the title): replays its recording tick for tick, any button returns"
if out=$(bash tools/make_attract.sh --check); then ok "make_attract.sh reproduces romdisk/demo/attract.in ($out)"; else bad "attract.in stale" "$out"; fi
T $B32 - "$W/att_rec.32" --state-only --story --route "$R/attract.route" --max-ticks 8000 > /dev/null 2>&1
run idle_attract
if route_ok "$W/idle_attract.32" && python3 - "$W/att_rec.32" "$W/idle_attract.32" <<'PYEOF'
import re, sys
def xs(path):
    out = []; cur = None
    for l in open(path):
        if l.startswith('F '): cur = l.split()[1]
        elif l.startswith('X ') and cur and cur != 'boot': out.append((int(cur), re.sub(r' fade=\d+', '', l.strip())))
    return out
rec = xs(sys.argv[1]); play = xs(sys.argv[2]); pd = dict(play)
start = next(t for t, l in play if l.startswith('X mode=field'))
bad = sum(1 for t, l in rec if pd.get(start + t - 1) != l)
print('demo from tick %d: %d ticks compared, %d differ' % (start, len(rec), bad), file=sys.stderr)
sys.exit(0 if rec and bad == 0 else 1)
PYEOF
then ok "the demo (idle 2,400 ticks) replays the recorded run tick for tick ($(grep -vc '^#' romdisk/demo/attract.in) input runs) and returns to the title"
else bad "attract demo replay"; fi

echo "== T3 budgets on every new camera (<= 18,000 written vertices, 0 drops, <= 1.60 M SH4 estimate)"
T $B32 - /dev/null --map /rd/maps/barrow.txt --sweep > "$W/sweep_barrow.txt"
sum=$(python3 tools/vs1art/sweep_map.py "$W/sweep_barrow.txt" romdisk/maps/barrow.txt)
python3 - "$W/sweep_barrow.txt" <<'PYEOF' && ok "the barrow, hero on every tile: $sum" || bad "barrow sweep" "$sum"
import sys
S = [dict(kv.split('=') for kv in l.split()[3:]) for l in open(sys.argv[1]) if l.startswith('S ')]
sys.exit(0 if S and max(int(s['arena']) for s in S) <= 18000 and sum(int(s['drops']) for s in S) == 0 and
         max(1.520 * (40 * int(s['xf']) + 24 * int(s['out'])) + 134813 for s in S) <= 1.60e6 else 1)
PYEOF
for p in title credits; do
  T $B32 - /dev/null --path $p > "$W/path_$p.txt"
  st=$(python3 - "$W/path_$p.txt" <<'PYEOF'
import sys
R = [dict(kv.split('=') for kv in l.split()[2:]) for l in open(sys.argv[1]) if l.startswith('V ')]
w = max(int(r['arena']) for r in R); d = sum(int(r['drops']) for r in R); e = max(float(r['est'].rstrip('M')) for r in R)
print('%d frames, worst %d written, %d drops, est worst %.2f M' % (len(R), w, d, e))
sys.exit(0 if w <= 18000 and d == 0 and e <= 1.60 else 1)
PYEOF
) && ok "the $p camera path: $st" || bad "$p path" "$st"
done
T $B32 - "$W/full_view.32" --state-only --view --boot --route "$R/full.route" --max-ticks 90000 > /dev/null 2>&1
vs=$(python3 - "$W/full_view.32" <<'PYEOF'
import sys, collections
st = collections.defaultdict(lambda: [0, 0, 0.0, 0])
for ln in open(sys.argv[1]):
    if ln.startswith('V '):
        d = dict(kv.split('=', 1) for kv in ln.split()[1:] if '=' in kv)
        s = st[d['mode']]; s[0] = max(s[0], int(d['arena'])); s[1] += int(d['drops'])
        s[2] = max(s[2], (1.520 * (40 * int(d['xf']) + 24 * int(d['out'])) + 134813) / 1e6); s[3] += 1
    elif ln.startswith('B '):
        d = dict(kv.split('=', 1) for kv in ln.split()[1:] if '=' in kv)
        s = st['battle']; s[0] = max(s[0], int(d['arena'])); s[1] += int(d['drops']); s[3] += 1
print(', '.join('%s %d/%d%s' % (m, s[0], s[1], ' %.2fM' % s[2] if s[2] else '') for m, s in sorted(st.items())))
sys.exit(0 if all(s[0] <= 18000 and s[1] == 0 and s[2] <= 1.60 for s in st.values()) else 1)
PYEOF
) && ok "the whole playthrough, every frame by mode (worst written/drops, est): $vs" || bad "playthrough budget" "$vs"

echo "== T12 resources and the heap"
printf '0.0166667 00\n' > "$W/one.in"
T $B32 "$W/one.in" "$W/res.trace" --state-only --resources > /dev/null
res=$(grep "^D resources" "$W/res.trace" | sed 's/^D resources: //')
python3 - "$res" <<'PYEOF' && ok "resources (plan 9): $res" || bad "resources over budget" "$res"
import re, sys
s = sys.argv[1]; v = lambda k: int(re.search(k, s).group(1))
# VRAM 1,280 KiB since the UI art pass (docs/UI_ART.md: the 512x256 UI atlas, 256 KiB; the PVR has ~4.4 MiB for textures)
sys.exit(0 if v(r'textures (\d+)/') <= 30 and v(r'VRAM (\d+) KiB') <= 1280 and v(r'materials (\d+)/') <= 200 and
         v(r'model arena (\d+) KiB') <= 1700 and v(r'script pools (\d+) B') <= 32 * 1024 else 1)
PYEOF
rd_bytes=$(find romdisk -type f -exec cat {} + | wc -c)
[ "$rd_bytes" -le 3100000 ] && ok "romdisk $rd_bytes bytes (<= 3.1 MB: + the UI atlas and the cel-study hulls)" || bad "romdisk $rd_bytes bytes"
g++ -std=gnu++17 -O2 -DHOST_ALLOC_COUNT $SHZ src/*.cpp platform/host/host_engine.cpp platform/host/trace_main.cpp platform/host/battle_bot.cpp \
    platform/host/story.cpp build/sh4zam-host/libsh4zam.a -o "$W/trace_alloc" 2> "$W/alloc_build.err" || bad "alloc build" "$(head -3 "$W/alloc_build.err")"
heap=""; for r in full cp_c3 idle_attract; do
  T "$W/trace_alloc" - "$W/alloc.trace" --state-only --boot --route "$R/$r.route" --max-ticks 90000 > /dev/null 2>&1
  heap="$heap $(grep '^D heap' "$W/alloc.trace" | grep -o '[0-9]* allocations')"; done
[ "$(echo "$heap" | tr -s ' ' | sed 's/^ //')" = "0 allocations 0 allocations 0 allocations" ] && \
  ok "no heap allocation inside any tick or draw after the boot (the playthrough, the boss retry, the demo: map changes, light lerps, the screens)" || bad "heap allocations:$heap"

echo "== T14 assets reproducible"
if out=$(python3 tools/make_logo.py --check); then ok "make_logo.py reproduces romdisk/tex/logo.tex"; else bad "logo.tex" "$out"; fi
if out=$(python3 tools/ground/paint_ground.py romdisk/maps/barrow.txt --check); then ok "paint_ground.py reproduces romdisk/maps/barrow.gnd ($(echo "$out" | grep -o 'VQ PSNR [0-9.]* dB'))"; else bad "barrow.gnd" "$out"; fi
snap="$W/props"; mkdir -p "$snap"; cp romdisk/models/{waystone,dead_tree,iron_fence,crypt,tomb_round,tomb_cross}.dmf "$snap/"
python3 tools/battle3d/make_props.py > /dev/null
same=1; for f in "$snap"/*.dmf; do cmp -s "$f" "romdisk/models/$(basename "$f")" || same=0; done
[ $same = 1 ] && ok "make_props.py reproduces the barrow's props (waystone, dead_tree, iron_fence) and the graveyard set" || bad "props differ"
if python3 tests/vs1art/check_chars.py 2>&1 | grep -q "^ok   corwin"; then ok "Corwin = VS1's ranger head + our body (tools/vs1art/build_chars.sh; tests/vs1art/check_chars.py)"; else bad "Corwin's model"; fi

if [ "$Q" != 1 ]; then
  echo "== sanitizers (ASan + UBSan)"
  g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $SHZ src/*.cpp platform/host/host_engine.cpp \
      platform/host/trace_main.cpp platform/host/battle_bot.cpp platform/host/story.cpp build/sh4zam-host/libsh4zam.a -o "$W/trace_asan" 2> "$W/asan_build.err" || bad "asan build" "$(head -5 "$W/asan_build.err")"
  asan_bad=""
  for r in full cp_c3 skip_ending idle_attract death_title; do
    ASAN_OPTIONS=detect_leaks=0 TMO=900 T "$W/trace_asan" - /dev/null --state-only --boot --route "$R/$r.route" --max-ticks 90000 > "$W/asan.out" 2>&1 || asan_bad="$asan_bad $r(exit)"
    grep -q "runtime error\|ERROR: AddressSanitizer" "$W/asan.out" && asan_bad="$asan_bad $r($(grep -m1 'runtime error\|AddressSanitizer' "$W/asan.out" | cut -c1-120))"
  done
  ASAN_OPTIONS=detect_leaks=0 T "$W/trace_asan" - /dev/null --state-only --route "$R/road_block.route" --max-ticks 90000 > "$W/asan.out" 2>&1 || asan_bad="$asan_bad road"
  [ -z "$asan_bad" ] && ok "ASan/UBSan clean: the playthrough, the boss retry, the skips, the demo, the title return, the road block" || bad "sanitizers:$asan_bad"
fi

echo "RESULT: pass=$pass fail=$fail"
[ $fail -eq 0 ] || { printf '  failed: %s\n' "${failed[@]}"; exit 1; }
