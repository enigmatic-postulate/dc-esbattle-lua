#!/usr/bin/env bash
# VS1 art reconciliation (docs/SLICE_VS1_ART.md) test suite. Runs from any directory; needs g++, python3 (numpy,
# pillow) and an SH4ZAM checkout (as `make host`). About 4 minutes.
#   tests/vs1art/run.sh
#   VS1PY=<python 3.12 with tools/vs1art/requirements-vs1.txt> KAYKIT=<KayKit dir> ...
#       also re-runs VS1's own character converter (vendored): it must reproduce VS1's uploaded skeleton and Mage
#       byte for byte, and the three characters VS1 did not ship (assets/vs1_art/models/) likewise
# The other suites (tests/dialog/run.sh, tests/battle3d/run.sh, tests/run_tests.sh) run separately.
set -u
cd "$(dirname "$0")/../.."
W="${WORK:-/tmp/vs1art_tests}"; rm -rf "$W"; mkdir -p "$W"
pass=0; fail=0; failed=()
ok()  { pass=$((pass+1)); echo "  PASS $1"; }
bad() { fail=$((fail+1)); failed+=("$1"); echo "  FAIL $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/       /' | head -14; }
T() { timeout "${TMO:-200}" "$@"; }
B32=./build/battle_trace_f32

echo "== build"
make -s host >/dev/null 2>&1 || { echo "host build failed"; exit 1; }
ok "host build"

echo "== the VS1 files, converted (tools/vs1art/import_props.py, import_chars.py)"
if out=$(python3 tools/vs1art/import_props.py --check); then ok "import_props.py reproduces the 10 textures and 13 props in romdisk/ byte for byte"; else bad "import_props.py --check" "$(echo "$out" | grep DIFF)"; fi
if out=$(python3 tests/vs1art/check_props.py); then ok "props and textures == VS1's (VS1's own reader): $(echo "$out" | grep -c '^ok') checks, every triangle where VS1 put it, RGB565 bit for bit, the vine's alpha thresholded"; else bad "props/textures vs VS1" "$(echo "$out" | grep FAIL)"; fi
if out=$(python3 tests/vs1art/check_chars.py); then ok "characters: the converter keeps every VS1 triangle (6 models); each shipped character = VS1's head parts + our body, our skeleton and clips, VS1's texture and eye glow"; else bad "characters" "$(echo "$out" | grep FAIL)"; fi
OUT="$W/chars" bash tools/vs1art/build_chars.sh > /dev/null
same=1; for n in knight skel_warrior skel_mage skel_rogue wren brom corwin; do cmp -s "$W/chars/$n.dmf" romdisk/models/$n.dmf || same=0; done
[ $same = 1 ] && ok "build_chars.sh reproduces the 7 shipped characters byte for byte (slice 3D-6: Corwin)" || bad "build_chars.sh output differs from romdisk/models"
if [ -n "${VS1PY:-}" ]; then
  KK="${KAYKIT:-../KayKit}"; AN="$KK/Animations"
  T "$VS1PY" tools/vs1art/vs1_fbx2dmf.py --model "$KK/Characters/Skeleton_Warrior.fbx" --texture "$KK/Characters/skeleton_texture.png" \
      --native-anim "$AN/Rig_Medium_Special.fbx:Skeletons_Idle:idle:15" --native-anim "$AN/Rig_Medium_Special.fbx:Skeletons_Walking:walk:15" \
      --unlit Glow=78FFE6 --max-error 0.025 -o "$W/skeleton.dmf" > /dev/null
  T "$VS1PY" tools/vs1art/vs1_fbx2dmf.py --model "$KK/Characters/Mage.fbx" --texture "$KK/Characters/mage_texture.png" \
      --native-anim "$AN/Rig_Medium_General.fbx:Idle_A:idle:15" --native-anim "$AN/Rig_Medium_MovementBasic.fbx:Walking_A:walk:15" \
      --max-error 0.02 --part-error Mage_Head=0.005 -o "$W/mage.dmf" > /dev/null
  cmp -s "$W/skeleton.dmf" assets/vs1_art/models/skeleton.dmf && cmp -s "$W/mage.dmf" assets/vs1_art/models/mage.dmf && \
    ok "the vendored VS1 converter reproduces VS1's uploaded skeleton.dmf and mage.dmf byte for byte" || bad "vendored VS1 converter differs from VS1's files"
  TMO=600 T env VS1PY="$VS1PY" KAYKIT="$KK" VS1OUT="$W/vs1" OUT="$W/chars2" bash tools/vs1art/build_chars.sh > /dev/null
  same=1; for n in barbarian skeleton_mage skeleton_rogue ranger; do cmp -s "$W/vs1/$n.dmf" assets/vs1_art/models/$n.dmf || same=0; done
  [ $same = 1 ] && ok "VS1's recipe rebuilds the Barbarian, Skeleton_Mage, Skeleton_Rogue and Ranger byte for byte" || bad "VS1-recipe characters differ"
fi

echo "== the village: VS1's layout and ground, the GDD's positions"
if out=$(python3 tests/vs1art/check_layout.py); then ok "$out"; else bad "layout" "$out"; fi
printf '0.0166667 00\n%.0s' $(seq 1 40) > "$W/idle40.in"
rm -rf "$W/gd" "$W/gt"; mkdir -p "$W/gd" "$W/gt"
T $B32 "$W/idle40.in" /dev/null --dump "$W/gd" --dump-frames 30 > /dev/null
T $B32 "$W/idle40.in" /dev/null --map /rd/maps/test_field.txt --dump "$W/gt" --dump-frames 30 > /dev/null
if out=$(python3 tests/vs1art/check_ground.py "$W/gt/frame_00030.txt" romdisk/maps/test_field.txt); then ok "maps without a painted ground: $out (test_field)"; else bad "generated ground texture" "$out"; fi

echo "== the painted village ground (docs/GROUND_ART.md)"
if out=$(python3 tools/ground/paint_ground.py --check); then ok "paint_ground.py reproduces romdisk/maps/village.gnd byte for byte ($(echo "$out" | grep -o 'VQ PSNR [0-9.]* dB'))"; else bad "paint_ground.py --check" "$out"; fi
if out=$(python3 tools/ground/paint_ground.py romdisk/maps/barrow.txt --check); then ok "paint_ground.py reproduces romdisk/maps/barrow.gnd byte for byte (slice 3D-6: the barrow palette; $(echo "$out" | grep -o 'VQ PSNR [0-9.]* dB'))"; else bad "paint_ground.py --check (barrow)" "$out"; fi
if out=$(python3 tests/vs1art/check_gnd.py "$W/gd/frame_00030.txt"); then ok "$(echo "$out" | tr '\n' ' ' | sed 's/  */ /g')"; else bad "painted ground" "$out"; fi

echo "== the field camera (VS1's: 9 up, 9 back, FOV 32, clamped to the map)"
TMO=300 T $B32 - "$W/r1v.trace" --state-only --view --route tests/dialog/routes/r1_canonical.route --max-ticks 90000 > /dev/null 2>&1
if out=$(python3 tests/vs1art/check_camera.py "$W/r1v.trace" 40 30); then ok "$out (route 1)"; else bad "field camera" "$out"; fi

echo "== frame cost with the hero on every walkable tile (budget: <= 18,000 written vertices, no drops, <= 1.60 M est.)"
T $B32 - /dev/null --sweep > "$W/sweep.txt"
sum=$(python3 tools/vs1art/sweep_map.py "$W/sweep.txt" romdisk/maps/village.txt)
if python3 - "$W/sweep.txt" <<'PYEOF'
import sys
S = [dict(kv.split('=') for kv in l.split()[3:]) for l in open(sys.argv[1]) if l.startswith('S ')]
worst = max(int(s['arena']) for s in S); drops = sum(int(s['drops']) for s in S)
est = max(1.520 * (40 * int(s['xf']) + 24 * int(s['out'])) + 134813 for s in S)
sys.exit(0 if S and worst <= 18000 and drops == 0 and est <= 1.60e6 else 1)
PYEOF
then ok "$sum"; else bad "sweep over budget" "$sum"; fi

echo "== the near-plane path (props under the camera) draws the same pixels as the former one"
TK=$(awk '/near=[1-9]/{print NR}' "$W/sweep.txt" | awk 'NR % 6 == 1' | head -8 | paste -sd,)
mkdir -p "$W/nab/vc_a_sweep" "$W/nab/vc_b_sweep"
T $B32 - /dev/null --sweep --dump "$W/nab/vc_a_sweep" --dump-frames "$TK" > /dev/null
T $B32 - /dev/null --sweep --tune near_strips=0 --dump "$W/nab/vc_b_sweep" --dump-frames "$TK" > /dev/null
n_a=$(awk '/near=[1-9]/' "$W/sweep.txt" | wc -l)
T $B32 - /dev/null --sweep --tune near_strips=0 > "$W/sweep0.txt"
w0=$(awk '{split($6,a,"="); if (a[2]>m) m=a[2]} END{print m}' "$W/sweep0.txt"); w1=$(awk '{split($6,a,"="); if (a[2]>m) m=a[2]} END{print m}' "$W/sweep.txt")
if out=$(python3 tests/battle3d/compare_dumps.py "$W/nab" romdisk romdisk); then
  ok "near-plane props as strip runs: $(echo "$TK" | tr ',' '\n' | wc -l) frames pixel-identical to the per-triangle path ($n_a tiles use it; worst frame $w0 -> $w1 written vertices)"
else bad "near-plane path changes pixels" "$out"; fi

echo "RESULT: pass=$pass fail=$fail"
[ $fail -eq 0 ] || { printf '  failed: %s\n' "${failed[@]}"; exit 1; }
