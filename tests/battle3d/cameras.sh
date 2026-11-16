#!/usr/bin/env bash
# Collects the camera of every frame of bot-played battles in an arena (intro played, outro held
# past its clamps), for tools/battle3d/view_cull.py:  tests/battle3d/cameras.sh village > cams.txt
# Re-run it (and tools/battle3d/build_assets.sh's view-cull step) whenever the battle cameras change;
# tests/battle3d/run.sh checks that the culled props still draw the same pixels in sampled battles.
set -eu
cd "$(dirname "$0")/../.."
ARENA="${1:-village}"
case "$ARENA" in
  village) ENCS="test_slimes slimes_2 guard_duel" ;;
  *) echo "unknown arena $ARENA" >&2; exit 2 ;;
esac
T=$(mktemp)
for enc in $ENCS; do
  for pol in cycle:AFG cycle:FGGA fire guard attack:0 attack:1 attack:2 cycle:bc cycle:cbA random:1 random:2 random:3 random:4 random:5 random:6; do
    for rules in game legacy; do
      ./build/battle_trace_f32 - "$T" --battle-test "$enc" --state-only --bot "$pol" --bot-wait 120 --bot-linger 200 --rules $rules --view > /dev/null
      grep "^B" "$T" | sed 's/.*cam=\([^ ]*\) tgt=\([^ ]*\) fov=\([^ ]*\).*/\1 \2 \3/'
    done
  done
done | sort -u
rm -f "$T"
