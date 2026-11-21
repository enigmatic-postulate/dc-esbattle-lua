#!/usr/bin/env bash
# Slice 3D-7 ("Sound, Save and Sign-off") test suite - docs/NEXT_SLICE_SOUND_SAVE.md 10. Runs from any directory;
# needs g++, python3 (numpy) and an SH4ZAM checkout (as `make host`). About 1 minute.
# So far: Stage A's host side (the codec, the stand-ins, the cue table, the audio logic). The other suites
# (tests/barrow, tests/dialog, tests/battle3d, tests/vs1art, tests/ui, tests/run_tests.sh) keep running separately.
set -u
cd "$(dirname "$0")/../.."
W="${WORK:-/tmp/sound_save_tests}"; rm -rf "$W"; mkdir -p "$W"
pass=0; fail=0; failed=()
ok()  { pass=$((pass+1)); echo "  PASS $1"; }
bad() { fail=$((fail+1)); failed+=("$1"); echo "  FAIL $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/       /' | head -14; }
T() { timeout "${TMO:-300}" "$@"; }
B32=./build/battle_trace_f32; B64=./build/battle_trace_f64
U="$W/audio_unit"

echo "== build (host), warnings, the KOS backend"
make -s host >/dev/null 2>&1 || { echo "host build failed"; exit 1; }
SHZ=$(make -s print-host-shz-flags)
g++ -std=gnu++17 -O1 -Wall -Wextra -Wshadow src/audio.cpp tests/sound_save/audio_unit.cpp -o "$U" 2> "$W/unit_build.err"
[ -x "$U" ] && [ ! -s "$W/unit_build.err" ] && ok "the audio logic builds alone with 0 warnings (no game, no renderer: pure logic)" || bad "audio_unit build" "$(head -6 "$W/unit_build.err")"
warn=$(g++ -std=gnu++17 -Wall -Wextra -Wshadow $SHZ -fsyntax-only src/audio.cpp platform/host/*.cpp 2>&1 | grep -c "warning\|error")
[ "$warn" = 0 ] && ok "src/audio.cpp and the host harness: 0 warnings (-Wall -Wextra -Wshadow)" || bad "warnings: $warn"
kw=0; nfl=0; for d in "" "-DBATTLE_DIAG=1" "-DBATTLE_DIAG=1 -DDIAG_SKIP_GAME" "-DBATTLE_DIAG=1 -DDIAG_SKIP_UPDATE" "-DNO_AUDIO=1" "-DNO_MUSIC=1" "-DBATTLE_DIAG=1 -DNO_MUSIC=1" "-DNO_FOG=1" "-DPVR_DEFAULTS=1 -DGROUND_NO_VQ=1"; do
  nfl=$((nfl+1)); for f in platform/kos/main.cpp platform/kos/audio_kos.cpp; do
  c=$(g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wshadow -Itests/dialog/kos_stub $SHZ $d $f 2>&1 | grep -c "warning\|error"); kw=$((kw+c)); done; done
[ "$kw" = 0 ] && ok "KOS-T1: the KOS backend (main.cpp, audio_kos.cpp) compiles against the stub headers in $nfl flavours (incl. NO_AUDIO=1, NO_MUSIC=1, DIAG)" || bad "KOS backend stub check: $kw warnings/errors"
grep -q "platform/kos/audio_kos.o" makefile && grep -q "ifdef NO_AUDIO" makefile && grep -q "ifdef NO_MUSIC" makefile && ok "the makefile builds platform/kos/audio_kos.o and has the NO_AUDIO / NO_MUSIC switches" || bad "makefile: audio_kos.o or a kill switch is missing"
grep -q "^.DEFAULT_GOAL := all" makefile && [ "$(grep -v '^-include' makefile | make -f - -p -n KOS_BASE=/nonexistent 2>/dev/null | grep -c '^.DEFAULT_GOAL := all')" = 1 ] && ok "a plain make builds 'all' (the .elf), not the first rule in the file" || bad "makefile: the default goal is not 'all'"
grep -q "src/audio.o" makefile && ok "the makefile builds src/audio.o" || bad "makefile: src/audio.o is missing"

echo "== AU-T1, AU-T4 the cue table: coverage, files, budgets; the loader == the lint"
if out=$(python3 tools/audio/lint_cues.py); then ok "$out"; else bad "lint_cues" "$out"; fi
d=0; n=0; for f in tests/sound_save/fixtures/*.txt; do n=$((n+1)); a=$("$U" --check "$f"); b=$(python3 tools/audio/lint_cues.py --table "$f")
  [ "$a" = "$b" ] || { d=$((d+1)); echo "       $f: [$a] vs [$b]"; }
  case "$(basename "$f")" in bad_*) [ -n "$a" ] || { d=$((d+1)); echo "       $f: accepted"; };; good*) [ -z "$a" ] || { d=$((d+1)); echo "       $f: $a"; };; esac; done
[ "$d" = 0 ] && ok "the C++ loader and lint_cues.py give the same verdict and message on $n fixtures" || bad "loader / lint parity: $d differences"

echo "== AU-T2 reproducible audio"
if out=$(python3 tools/audio/make_standins.py --check); then ok "every .adp on the romdisk is rebuilt byte for byte ($out)"; else bad "stand-ins differ from the generator" "$out"; fi

echo "== AU-T3 codec parity and loop seams"
"$U" --decode romdisk/audio/music/*.adp romdisk/audio/sfx/*.adp > "$W/cpp.dec"
python3 tools/audio/adpcm.py romdisk/audio/music/*.adp romdisk/audio/sfx/*.adp > "$W/py.dec"
nf=$(wc -l < "$W/py.dec")
cmp -s "$W/py.dec" "$W/cpp.dec" && [ "$nf" -ge 42 ] && ok "adpcm.py == src/adpcm.h, sample for sample, on $nf files" || bad "decoders differ" "$(diff "$W/py.dec" "$W/cpp.dec" | head -6)"
loops=$(grep -c "seam=ok" "$W/cpp.dec"); mism=$(grep -c "MISMATCH\|BAD\|MISSING" "$W/cpp.dec")
[ "$mism" = 0 ] && [ "$loops" -ge 6 ] && ok "$loops looped tracks: the decoder is at its key-on state at the loop start and at the loop end (every pass decodes alike)" || bad "loop seams" "$(grep "MISMATCH\|BAD\|MISSING" "$W/cpp.dec")"
if out=$(python3 - <<'PY'
import math, sys
sys.path.insert(0, 'tools/audio')
import adpcm
n = 22050
res = []
for f, amp, floor in ((110, 20000, 45.0), (440, 12000, 34.0), (1760, 6000, 22.0)):
    pcm = [int(amp * math.sin(2 * math.pi * f * i / 22050.0) + 0.2 * amp * math.sin(2 * math.pi * 2.7 * f * i / 22050.0)) for i in range(n)]
    data, info = adpcm.encode(pcm)
    out, _ = adpcm.decode(data, n)
    err = sum((a - b) ** 2 for a, b in zip(pcm, out)) or 1
    snr = 10 * math.log10(sum(a * a for a in pcm) / err)
    res.append((f, snr, snr >= floor))
print('encoder SNR on test tones: ' + ', '.join('%d Hz %.1f dB' % (f, s) for f, s, _ in res) + ' (4 bits a sample: it falls with pitch)')
sys.exit(0 if all(o for _, _, o in res) else 1)
PY
); then ok "$out"; else bad "encoder quality" "$out"; fi

echo "== AU-T8 and the logic: the frame queue, voices, music, ducking, options"
rm -rf "$W/unit_rd"; mkdir -p "$W/unit_rd/audio"; ln -s "$(pwd)/romdisk/audio/music" "$(pwd)/romdisk/audio/sfx" "$W/unit_rd/audio/"; cp tests/sound_save/unit_cues.txt "$W/unit_rd/audio/cues.txt"
"$U" --logic "$W/unit_rd" > "$W/logic.log" 2>&1
"$U" --logic romdisk 2>&1 | grep -q "ok   boot: the cue table and every header load" && ok "the game's own cue table boots in the unit harness (every file's header reads)" || bad "the game's cue table does not boot"
grep -q "^RESULT: pass=[0-9]* fail=0" "$W/logic.log" && ok "audio logic: $(grep -c '^  ok' "$W/logic.log") checks (burst frame <= 8 commands, one blip per 3 ticks, voice priorities, fades on ticks, stings, ducking, options, pan)" \
  || bad "audio logic" "$(grep "FAIL" "$W/logic.log")"

echo "== AU-T7 audio never changes the game"
rm -rf "$W/rd_silent"; mkdir "$W/rd_silent"; for e in romdisk/*; do [ "$(basename "$e")" = audio ] || ln -s "$(pwd)/$e" "$W/rd_silent/"; done
for b in 32 64; do
  eval "B=\$B$b"
  T $B - "$W/on.$b" --state-only --boot --route tests/barrow/routes/full.route --max-ticks 90000 > /dev/null 2>&1
  T $B - "$W/off.$b" --state-only --boot --route tests/barrow/routes/full.route --max-ticks 90000 --romdisk "$W/rd_silent" > /dev/null 2>&1
done
grep -q "^E route done: [0-9]* expectations, 0 failures" "$W/on.32" || bad "full.route with audio" "$(grep '^E route' "$W/on.32")"
ac=$(grep -c "^AC " "$W/on.32"); acoff=$(grep -c "^AC " "$W/off.32")
grep -v "^AC " "$W/on.32" | cmp -s - "$W/off.32" && [ "$ac" -gt 100 ] && [ "$acoff" = 0 ] && \
  ok "the playthrough's trace with audio == without (a romdisk with no audio/), except its $ac AC lines" || bad "audio changed the trace" "$(grep -v '^AC ' "$W/on.32" | cmp - "$W/off.32" 2>&1)"
cmp -s "$W/on.32" "$W/on.64" && ok "float == double, the backend commands included" || bad "float != double with audio" "$(cmp "$W/on.32" "$W/on.64")"
mus=$(grep "^AC music" "$W/on.32" | awk '{print $3}' | sort -u | tr '\n' ' ')
miss=""; for m in title village barrow battle boss dawn victory; do echo " $mus" | grep -q " $m " || miss="$miss $m"; done
[ -z "$miss" ] && ok "the playthrough starts these tracks: $mus" || bad "tracks never started:$miss" "$mus"
unk=$(grep -c "^A " "$W/on.32"); pl=$(grep -c "^AC play" "$W/on.32")
[ "$pl" -gt 50 ] && ok "$unk events -> $pl effects played, $(grep -c '^AC mvol' "$W/on.32") volume steps" || bad "few effects played: $pl"

echo
echo "RESULT: pass=$pass fail=$fail"
[ "$fail" = 0 ] || { printf 'failed: %s\n' "${failed[@]}"; exit 1; }
