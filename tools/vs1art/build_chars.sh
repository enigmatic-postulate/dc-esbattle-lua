#!/usr/bin/env bash
# Builds the shipped characters: VS1's head parts on our bodies (docs/SLICE_VS1_ART.md, "Characters").
#   tools/vs1art/build_chars.sh            the merges only (python3 + numpy + pillow): byte-identical outputs
#   VS1PY=<python 3.12 with tools/vs1art/requirements-vs1.txt> KAYKIT=<KayKit dir> tools/vs1art/build_chars.sh
#                                          also re-runs VS1's converter (tools/vs1art/vs1_fbx2dmf.py) for the
#                                          characters VS1 did not ship (Barbarian, Skeleton_Mage, Skeleton_Rogue),
#                                          with VS1's recipe, into assets/vs1_art/models/ (byte-identical)
# Inputs: assets/vs1_art/models/<vs1>.dmf (VS1's hero, skeleton, mage as uploaded + the three above) and
# assets/vs1_art/bodies/<ours>.dmf (our pre-VS1 characters: their bodies, clips and rest pose are kept).
# Outputs: romdisk/models/{knight,skel_warrior,skel_mage,skel_rogue,wren,brom}.dmf (OUT=dir: there instead)
set -euo pipefail
cd "$(dirname "$0")/../.."
A=assets/vs1_art/models
if [ -n "${VS1PY:-}" ]; then
  A="${VS1OUT:-$A}"; mkdir -p "$A"           # VS1OUT=dir: write the three there (the merges below still read assets/)
  KK="${KAYKIT:-../KayKit}"
  AN="$KK/Animations"
  # VS1's recipe (larp3-2/tools/dmf/build_characters.sh): the body at 0.02-0.025 model units, the face at
  # 0.005; skeletons with the cyan unlit eye glow and their native idle (only the head parts are used here)
  "$VS1PY" tools/vs1art/vs1_fbx2dmf.py --model "$KK/Characters/Barbarian.fbx" --texture "$KK/Characters/barbarian_texture.png" \
      --native-anim "$AN/Rig_Medium_General.fbx:Idle_A:idle:15" --max-error 0.02 --part-error Barbarian_Head=0.005 -o "$A/barbarian.dmf" > /dev/null
  "$VS1PY" tools/vs1art/vs1_fbx2dmf.py --model "$KK/Characters/Skeleton_Mage.fbx" --texture "$KK/Characters/skeleton_texture.png" \
      --native-anim "$AN/Rig_Medium_Special.fbx:Skeletons_Idle:idle:15" --unlit Glow=78FFE6 --max-error 0.025 -o "$A/skeleton_mage.dmf" > /dev/null
  "$VS1PY" tools/vs1art/vs1_fbx2dmf.py --model "$KK/Characters/Skeleton_Rogue.fbx" --texture "$KK/Characters/skeleton_texture.png" \
      --native-anim "$AN/Rig_Medium_Special.fbx:Skeletons_Idle:idle:15" --unlit Glow=78FFE6 --max-error 0.025 -o "$A/skeleton_rogue.dmf" > /dev/null
  # slice 3D-6: the Ranger (Corwin), the Mage's recipe: the body at 0.02, the face at 0.005
  "$VS1PY" tools/vs1art/vs1_fbx2dmf.py --model "$KK/Characters/Ranger.fbx" --texture "$KK/Characters/ranger_texture.png" \
      --native-anim "$AN/Rig_Medium_General.fbx:Idle_A:idle:15" --max-error 0.02 --part-error Ranger_Head=0.005 -o "$A/ranger.dmf" > /dev/null
  A=assets/vs1_art/models
fi
B=assets/vs1_art/bodies
M="${OUT:-romdisk/models}"; mkdir -p "$M"
py() { python3 tools/vs1art/import_chars.py "$@"; }
py hero           "$M/knight.dmf"       --body "$B/knight.dmf"         # Rowan: VS1's crested helm and face
py skeleton       "$M/skel_warrior.dmf" --body "$B/skel_warrior.dmf"   # patrols: skull, jaw, glowing eyes, spiked helm
py skeleton_mage  "$M/skel_mage.dmf"    --body "$B/skel_mage.dmf"      # the Bone Warden
py skeleton_rogue "$M/skel_rogue.dmf"   --body "$B/skel_rogue.dmf"     # the battle's skull rogue
py mage           "$M/wren.dmf"         --body "$B/wren.dmf"           # Wren: VS1's hat and face
py barbarian      "$M/brom.dmf"         --body "$B/brom.dmf"           # Brom: the bear hood and face
py ranger         "$M/corwin.dmf"       --body "$B/corwin.dmf"         # Corwin (slice 3D-6): the Ranger's hair and face
