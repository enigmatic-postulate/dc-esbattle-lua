#!/usr/bin/env bash
# Rebuilds every asset of the dialog slice (3D-5) from its sources. Byte-for-byte reproducible
# (tests/dialog/run.sh checks it; with BPY= also the Blender-made ones).
#   BPY=<python with bpy 4.2> KAYKIT=<KayKit dir> tools/dialog/build_assets.sh
# Blender's Python: uv venv -p 3.11 .venv && uv pip install --python .venv/bin/python bpy==4.2.0 numpy pillow
# The animation library (with the slice's `walk` clip) is built by tools/battle3d/build_assets.sh.
set -euo pipefail
cd "$(dirname "$0")/../.."
BPY="${BPY:-.venv/bin/python}"
KAYKIT="${KAYKIT:-../KayKit}"
R=tools/dialog/reports
HI=build/portrait_src
mkdir -p romdisk/models romdisk/portraits romdisk/fonts "$R" "$HI"
quiet() { "$@" 2>&1 | grep -v -E '^(FBX version|$)' || true; }

# the dialog font (pure Python: fontTools; the outlines are filled exactly on the pixel grid)
python3 tools/dialog/make_font.py

# Wren (the human Mage) and Brom (the Barbarian), field NPCs on the shared animation library. Budgets chosen
# with tools/battle3d/qc_model.py (docs/SLICE_DIALOG.md 4): Wren's wide hat needs 1,100 triangles, Brom 950.
# Since the VS1 art reconciliation these are the BODIES (assets/vs1_art/bodies/): tools/vs1art/build_chars.sh
# puts VS1's head parts on them and writes romdisk/models/wren.dmf and brom.dmf.
B=assets/vs1_art/bodies
mkdir -p "$B"
quiet "$BPY" tools/battle3d/convert_model.py --fbx "$KAYKIT/Characters/Mage.fbx" --texture "$KAYKIT/Characters/mage_texture.png" \
    --tris 1100 --out "$B/wren.dmf" --report "$R/wren.json"
quiet "$BPY" tools/battle3d/convert_model.py --fbx "$KAYKIT/Characters/Barbarian.fbx" --texture "$KAYKIT/Characters/barbarian_texture.png" \
    --tris 950 --out "$B/brom.dmf" --report "$R/brom.json"

# portraits: high-detail conversions of the same FBX (build products, decision D10), then the renderer
for c in Knight:knight Mage:mage Barbarian:barbarian; do
  quiet "$BPY" tools/battle3d/convert_model.py --fbx "$KAYKIT/Characters/${c%%:*}.fbx" --texture "$KAYKIT/Characters/${c##*:}_texture.png" \
      --tris 9000 --tex-size 512 --out "$HI/${c##*:}_hi.dmf" --report "$HI/${c##*:}_hi.json"
done
python3 tools/dialog/make_portraits.py --hi "$HI"
