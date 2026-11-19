#!/usr/bin/env bash
# Rebuilds romdisk/models/hull/*.dmf (the cel-shading prototype's decimated outline hulls, docs/CEL_SHADING_STUDY.md):
# each character welded, decimated by Blender's quadric collapse to 18 % (tools/cel/bpy_decimate.py), re-stripped.
# Needs Blender's Python with bpy (BLENDER_PY, default /tmp/bpyenv/bin/python). Deterministic: byte-identical output.
set -eu
cd "$(dirname "$0")/../.."
OUT="${1:-romdisk/models/hull}"
for m in knight wren brom corwin skel_warrior skel_rogue skel_mage; do
  python3 tools/cel/hull_lab.py "$m" --cells '' --quadric 0.18 --write "$OUT" | tail -1
done
