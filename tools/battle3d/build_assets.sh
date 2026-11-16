#!/usr/bin/env bash
# Rebuilds every asset of the 3D battle (slice 3D-4) from the KayKit sources. Byte-for-byte
# reproducible (tests/battle3d/run.sh checks it when Blender's Python is available).
#   BPY=<python with bpy 4.2> KAYKIT=<KayKit dir> tools/battle3d/build_assets.sh
# Blender's Python: uv venv -p 3.11 .venv && uv pip install --python .venv/bin/python bpy==4.2.0 numpy pillow
set -euo pipefail
cd "$(dirname "$0")/../.."
BPY="${BPY:-.venv/bin/python}"
KAYKIT="${KAYKIT:-../KayKit}"
R=tools/battle3d/reports
mkdir -p romdisk/anims romdisk/models "$R"
quiet() { "$@" 2>&1 | grep -v -E '^(FBX version|$)' || true; }

# shared animation library (every Rig_Medium character)
quiet "$BPY" tools/battle3d/build_anims.py --kaykit "$KAYKIT" --out romdisk/anims/rig_medium.dma --report "$R/anims.json"

# the skeleton squad's third member (Warrior and Mage are the field models, unchanged)
quiet "$BPY" tools/battle3d/convert_model.py --fbx "$KAYKIT/Characters/Skeleton_Rogue.fbx" \
    --texture "$KAYKIT/Characters/skeleton_texture.png" --tris 800 --glow-color 9CF0FF --out romdisk/models/skel_rogue.dmf --report "$R/skel_rogue.json"

# weapons, attached to the hand slots at runtime and drawn with the holder's texture
weapon() {   # source atlas tris out
    quiet "$BPY" tools/battle3d/convert_model.py --weapon --fbx "$KAYKIT/Weapons/$1.fbx" \
        --texture "$KAYKIT/Characters/$2_texture.png" --tris "$3" --out "romdisk/models/$4.dmf" --report "$R/$4.json"
}
weapon sword_1handed           knight   80 w_sword
weapon shield_badge_color      knight  200 w_shield   # keeps the quartered badge (110 lost it)
weapon Skeleton_Shield_Large_A skeleton 120 w_skel_shield
weapon Skeleton_Blade          skeleton  90 w_skel_blade

# FX atlas, the arena's dirt, the barrow's stand-in graveyard props (procedural)
python3 tools/battle3d/make_fx.py
python3 tools/battle3d/make_props.py

# the village arena's houses with only the triangles a battle camera can see (slow: ~10 minutes).
# Re-run after changing the battle cameras; tests/battle3d/run.sh checks the pixels still match.
if [ "${VIEW_CULL:-0}" = 1 ]; then
  bash tests/battle3d/cameras.sh village > /tmp/b3_cams_village.txt
  python3 tools/battle3d/view_cull.py romdisk/arenas/village.txt house_stone_b /tmp/b3_cams_village.txt romdisk/models/house_stone_b.dmf --source house_stone_r0 --every 2 &
  python3 tools/battle3d/view_cull.py romdisk/arenas/village.txt house_plaster_b /tmp/b3_cams_village.txt romdisk/models/house_plaster_b.dmf --source house_plaster_r0 --every 2 &
  wait
fi
