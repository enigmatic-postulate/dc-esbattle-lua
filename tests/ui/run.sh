#!/usr/bin/env bash
# The UI art pass (docs/UI_ART.md): the anti-aliased UI font and the rendered icons (romdisk/ui/ui.uia) replace the 5x7
# bitmap font and the 16x16 pixel icons of the 2D battle engine in the 3D game. About 5 minutes.
#   tests/ui/run.sh
# Checks: the build (0 warnings, the KOS backend), the atlas (reproducible, loads, the fallback without it), the 3D game
# no longer calls the old font or icons, the 2D cost (sprites per frame: no more than before, per scenario), the KOS
# sprite queues never grow (no heap after the boot on the Dreamcast), the 3D frame cost unchanged, no heap after boot.
set -u
cd "$(dirname "$0")/../.."
W="${WORK:-/tmp/ui_tests}"; rm -rf "$W"; mkdir -p "$W"
pass=0; fail=0; failed=()
ok()  { pass=$((pass+1)); echo "  PASS $1"; }
bad() { fail=$((fail+1)); failed+=("$1"); echo "  FAIL $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/       /' | head -14; }
T() { timeout "${TMO:-600}" "$@"; }
B32=./build/battle_trace_f32; B64=./build/battle_trace_f64

echo "== build (host, float + double), warnings, the KOS backend"
make -s host >/dev/null 2>&1 || { echo "host build failed"; exit 1; }
SHZ=$(make -s print-host-shz-flags)
warn=$(g++ -std=gnu++17 -Wall -Wextra -Wshadow $SHZ -fsyntax-only src/*.cpp platform/host/*.cpp 2>&1 | grep -c "warning\|error")
[ "$warn" = 0 ] && ok "src and the host harness build with 0 warnings (-Wall -Wextra -Wshadow)" || bad "warnings: $warn"
kw=0; for d in "" "-DBATTLE_DIAG=1" "-DNO_FOG=1" "-DCEL_DEFAULT=1" "-DPVR_DEFAULTS=1 -DGROUND_NO_VQ=1"; do
  c=$(g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wshadow -Itests/dialog/kos_stub $SHZ $d platform/kos/main.cpp 2>&1 | grep -c "warning\|error"); kw=$((kw+c)); done
[ "$kw" = 0 ] && ok "the KOS backend compiles against stub KOS headers in 5 flavours" || bad "KOS backend stub check: $kw warnings/errors"
grep -q "src/uitext.o" makefile && ok "the makefile builds src/uitext.o" || bad "makefile: src/uitext.o missing"

echo "== the atlas"
if out=$(python3 tools/ui/make_ui_atlas.py --check 2>&1); then ok "make_ui_atlas.py reproduces romdisk/ui/ui.uia byte for byte (LZ16 round trip asserted)"; else bad "atlas not reproducible" "$out"; fi
printf '0.0166667 00\n' > "$W/one.in"
T $B32 "$W/one.in" "$W/res.trace" --state-only --resources > /dev/null 2>&1
res=$(grep "^D resources" "$W/res.trace" | sed 's/^D resources: //')
if ! grep -q "^D ui:" "$W/res.trace" && python3 - "$res" <<'PYEOF'
import re, sys
s = sys.argv[1]; v = lambda k: int(re.search(k, s).group(1))
sys.exit(0 if v(r'textures (\d+)/') <= 30 and v(r'VRAM (\d+) KiB') <= 1280 else 1)
PYEOF
then ok "the atlas loads at boot (1 texture slot, 256 KiB VRAM): $res"; else bad "the atlas did not load or is over budget" "$(grep '^D ui:' "$W/res.trace") $res"; fi
sz=$(wc -c < romdisk/ui/ui.uia)
[ "$sz" -le 65536 ] && ok "romdisk/ui/ui.uia is $sz bytes (the 256 KiB of texels LZ16-packed; the romdisk lives in RAM)" || bad "ui.uia $sz bytes"

echo "== the fallback (no atlas: the old font and icons) and determinism (the UI never touches the game state)"
rm -rf "$W/rd_noui"; cp -r romdisk "$W/rd_noui"; rm -f "$W/rd_noui/ui/ui.uia"
T $B32 - "$W/full.st" --state-only --boot --route tests/barrow/routes/full.route --max-ticks 90000 > /dev/null 2>&1
T $B32 - "$W/full_noui.st" --state-only --boot --route tests/barrow/routes/full.route --max-ticks 90000 --romdisk "$W/rd_noui" > /dev/null 2>&1
T $B64 - "$W/full.st64" --state-only --boot --route tests/barrow/routes/full.route --max-ticks 90000 > /dev/null 2>&1
if grep -q "^D ui: /rd/ui/ui.uia missing" "$W/full_noui.st" && grep -q "^E route done: [0-9]* expectations, 0 failures" "$W/full_noui.st" &&
   cmp -s <(grep -v "^D" "$W/full.st") <(grep -v "^D" "$W/full_noui.st"); then
  ok "without the atlas the playthrough runs on the old font and icons, state trace identical to the atlas run"
else bad "fallback" "$(grep '^D ui\|^E route' "$W/full_noui.st" | head -3)"; fi
cmp -s <(grep -v "^D" "$W/full.st") <(grep -v "^D" "$W/full.st64") && ok "float == double over the playthrough with the new UI" || bad "float != double"

echo "== the 3D game no longer draws with the 5x7 font or the 16x16 pixel icons"
# legacy: the 2D battle view (battle.cpp's sprite view and banner, ui.cpp, fx.cpp, presentation.cpp), the input probe
# (a debug overlay), the old modules themselves and the fallback in uitext.cpp
hits=$(grep -nw "font_draw\|font_draw_centered\|font_width\|icon_draw\|icon_draw_tint" src/*.cpp; grep -n "ui_draw_prompts(" src/*.cpp)
hits=$(echo "$hits" | grep -v "^$" | grep -v "^src/\(battle\|ui\|fx\|presentation\|input_probe\|font\|icons\|uitext\)\.cpp:")
[ -z "$hits" ] && ok "no font_draw / icon_draw / ui_draw_prompts outside the legacy 2D view and the fallback" || bad "old font/icons still used" "$hits"

echo "== 2D cost: sprites per frame (each ~60 game + ~200 KOS instructions) no more than before the pass"
# frozen from the build before the pass (tools/ui/sprite_cost.py --old-exe ...): worst / mean sprites per frame
cat > "$W/base.txt" <<'EOF'
ui3d 120 90.3
squad 148 47.0
warden 301 120.7
full 693 132.2
cp_c3 693 130.0
EOF
spr() {   # NAME args...: worst and mean sprites per frame of a draw trace
  local n="$1"; shift
  T $B32 "$@" > /dev/null 2>&1
  python3 - "$W/$n.tr" <<'PYEOF'
import sys
fr, s, r = [], 0, 0          # frames with neither sprites nor rects are not counted (as tools/ui/sprite_cost.py)
for ln in open(sys.argv[1]):
    if ln.startswith('S '): s += 1
    elif ln.startswith('R '): r += 1
    elif ln.startswith('F '): fr.append((s, r)); s = r = 0
fr.append((s, r)); fr = [x[0] for x in fr if x != (0, 0)] or [0]
print(max(fr), round(sum(fr) / len(fr), 1))
PYEOF
  rm -f "$W/$n.tr"
}
echo "ui3d $(spr ui3d tests/battle3d/ui3d.in "$W/ui3d.tr" --battle-test test_slimes)" > "$W/new.txt"
echo "squad $(spr squad - "$W/squad.tr" --battle-test test_slimes --bot cycle:AFG --bot-wait 120 --bot-linger 120)" >> "$W/new.txt"
echo "warden $(spr warden - "$W/warden.tr" --battle-test warden --bot boss:good:2 --bot-wait 120 --bot-linger 120)" >> "$W/new.txt"
echo "full $(spr full - "$W/full.tr" --boot --route tests/barrow/routes/full.route --max-ticks 90000)" >> "$W/new.txt"
echo "cp_c3 $(spr cp_c3 - "$W/cp_c3.tr" --boot --route tests/barrow/routes/cp_c3.route --max-ticks 90000)" >> "$W/new.txt"
if out=$(python3 - "$W/base.txt" "$W/new.txt" <<'PYEOF'
import sys
b = {l.split()[0]: (int(l.split()[1]), float(l.split()[2])) for l in open(sys.argv[1])}
n = {l.split()[0]: (int(l.split()[1]), float(l.split()[2])) for l in open(sys.argv[2])}
rows = ['%s %d->%d worst, %.0f->%.0f mean' % (k, b[k][0], n[k][0], b[k][1], n[k][1]) for k in b]
print('; '.join(rows))
sys.exit(0 if all(n[k][0] <= b[k][0] and n[k][1] <= b[k][1] for k in b) else 1)
PYEOF
); then ok "sprites per frame before -> after: $out"; else bad "more sprites than before" "$out"; fi

echo "== the KOS sprite queues never grow (a growth = a heap allocation after the boot on the Dreamcast)"
qbad=""
for r in full cp_c3 idle_attract; do
  T $B32 - "$W/q_$r.tr" --state-only --boot --route tests/barrow/routes/$r.route --max-ticks 90000 --sprite-queues > /dev/null 2>&1
  grep -q "^D sprite queues: overflow draws 0;" "$W/q_$r.tr" || qbad="$qbad $r($(grep '^D sprite queues' "$W/q_$r.tr" | cut -c1-120))"
done
for b in "test_slimes cycle:AFG" "warden boss:good:2"; do set -- $b
  T $B32 - "$W/q_b.tr" --state-only --battle-test "$1" --bot "$2" --bot-wait 120 --bot-linger 120 --sprite-queues > /dev/null 2>&1
  grep -q "^D sprite queues: overflow draws 0;" "$W/q_b.tr" || qbad="$qbad $1"
done
[ -z "$qbad" ] && ok "0 overflow draws over the playthrough, the boss retry, the demo and two battles ($(grep -o 'tex [0-9]* peak [0-9]*/[0-9]* nearest' "$W/q_full.tr" | sort -t' ' -k4 -n -r | head -1); the UI atlas: $(grep -o '[0-9]*/1024 bilinear' "$W/q_full.tr" | head -1))" || bad "sprite queue overflow:$qbad"

echo "== the 3D frame cost is unchanged (the pass is 2D only)"
# frozen worst frames (written vertices / the SH4 estimate) of the battle budget runs before the pass
b3=$(for b in "test_slimes cycle:AFG 17422" "warden boss:good:2 14609"; do set -- $b
  T $B32 - "$W/b3.tr" --state-only --battle-test "$1" --bot "$2" --bot-wait 120 --bot-linger 120 --view > /dev/null 2>&1
  w=$(awk '/^B /{for(i=2;i<=NF;i++){split($i,a,"="); if(a[1]=="arena" && a[2]>m) m=a[2]}} END{print m+0}' "$W/b3.tr")
  echo "$1:$w/$3"; done)
echo "$b3" | awk -F'[:/]' '$2 != $3 {bad=1} END {exit bad}' && ok "battle worst frames identical to before: $(echo $b3)" || bad "3D cost changed" "$b3"

echo "== no heap allocation after the boot (the atlas is unpacked into boot-time buffers, freed)"
g++ -std=gnu++17 -O2 -DHOST_ALLOC_COUNT $SHZ src/*.cpp platform/host/host_engine.cpp platform/host/trace_main.cpp platform/host/battle_bot.cpp \
    platform/host/story.cpp build/sh4zam-host/libsh4zam.a -o "$W/trace_alloc" 2> "$W/alloc_build.err" || bad "alloc build" "$(head -3 "$W/alloc_build.err")"
heap=""
T "$W/trace_alloc" - "$W/al.tr" --state-only --boot --route tests/barrow/routes/full.route --max-ticks 90000 > /dev/null 2>&1
heap="$heap $(grep '^D heap' "$W/al.tr" | grep -o '[0-9]* allocations')"
T "$W/trace_alloc" - "$W/al2.tr" --state-only --battle-test warden --bot boss:good:2 --bot-wait 120 --bot-linger 120 > /dev/null 2>&1
heap="$heap $(grep '^D heap' "$W/al2.tr" | grep -o '[0-9]* allocations')"
heap=$(echo "$heap" | tr -s ' ' | sed 's/^ //')
[ "$heap" = "0 allocations 0 allocations" ] && ok "0 heap allocations in any tick or draw after the boot (the playthrough, the boss battle)" || bad "heap after boot: $heap"

echo "RESULT: pass=$pass fail=$fail"
[ $fail -eq 0 ] || { printf '  failed: %s\n' "${failed[@]}"; exit 1; }
