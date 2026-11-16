#!/usr/bin/env bash
# The Bone Warden's balance bots (GDD 12.6): 50 seeds each of good / naive / noguard play.
#   tests/battle3d/boss_balance.sh [--set "warden.hp=40,..."] [seeds]
# Prints the win rate, the hero's mean HP at a win, hero turns, and how 'noguard' losses happen.
# Runs the sprite view (--view2d): the rules are the same (tests/battle3d/run.sh proves it), 10x faster.
set -u
cd "$(dirname "$0")/../.."
SET=""; [ "${1:-}" = "--set" ] && { SET="--set $2"; shift 2; }
N="${1:-50}"
T=$(mktemp -d)
for mode in good naive noguard; do
  w=0; hps=0; turns=0; rend_deaths=0
  for seed in $(seq 1 "$N"); do
    ./build/battle_trace_f64 - /dev/null --battle-test warden --state-only --view2d --bot "boss:$mode:$seed" $SET --turn-log "$T/l" > /dev/null
    o=$(sed -n 's/^END outcome=\([a-z]*\).*/\1/p' "$T/l")
    turns=$((turns + $(grep -c "^ACT [0-9]* hero" "$T/l")))
    if [ "$o" = won ]; then
      w=$((w+1)); hp=$(grep "^ST" "$T/l" | tail -1 | sed 's/.*hero:\([0-9]*\)\/.*/\1/'); hps=$((hps + hp))
    elif grep "^ACT" "$T/l" | tail -1 | grep -q soul_rend; then rend_deaths=$((rend_deaths+1)); fi
  done
  printf "%-8s won %2d/%d (%3d%%)  mean HP at a win %s  mean hero turns %d  losses to Soul Rend %d\n" "$mode" "$w" "$N" $((100*w/N)) \
    "$([ $w -gt 0 ] && echo $((hps/w)) || echo -)" $((turns/N)) "$rend_deaths"
done
rm -rf "$T"
