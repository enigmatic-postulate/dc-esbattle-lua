# assets/vs1_art - the VS1 art, as uploaded, and what it is built into

Source of the village and character art adopted in [docs/SLICE_VS1_ART.md](../../docs/SLICE_VS1_ART.md).
"VS1" is the sibling implementation uploaded as `larp2_3d_vs1.zip` (`larp3-2/`): the same GDD, the same asset
kits (KayKit characters, Quaternius Medieval Village MegaKit, Poly Haven boulders - all CC0), a different
pipeline and runtime. Its files are a different DMF variant (24-byte vertices, pre-twiddled `.dtx` textures,
per-yaw variants pre-rotated into the world, baked lighting in the normal bytes); `tools/vs1art/` converts them.

| Here | From the upload | Used for |
|---|---|---|
| `models/village/*.dmf` (13) | `larp3-2/romdisk/models/village/` | the field's houses, arch, door steps, boulders, wagon, crates, fences (`tools/vs1art/import_props.py` -> `romdisk/models/<name>.dmf`) |
| `textures/*.dtx` (10) | `larp3-2/romdisk/textures/` | `romdisk/tex/*.png` (shared by the field props and, same UVs, the battle arena's houses) |
| `models/props.txt` | `larp3-2/romdisk/models/props.txt` | footprints / blocking / fading of the catalog (the map conversion, `tests/vs1art/check_layout.py`) |
| `village_map_vs1.txt` | `larp3-2/romdisk/maps/village.txt` | the layout and tiles of `romdisk/maps/village.txt` |
| `models/hero.dmf`, `skeleton.dmf`, `mage.dmf` | `larp3-2/romdisk/models/` | the head parts of Rowan, the skeleton warrior, Wren (`tools/vs1art/import_chars.py`) |
| `models/barbarian.dmf`, `skeleton_mage.dmf`, `skeleton_rogue.dmf` | **not in the upload**: built here with VS1's own converter (`tools/vs1art/vs1_fbx2dmf.py`, vendored) and VS1's recipe from the KayKit FBX | the head parts of Brom, the Bone Warden, the skull rogue |
| `models/ranger.dmf` | **not in the upload** (slice 3D-6): VS1's converter and the Mage's recipe on the KayKit Ranger (CC0) | the head part of Corwin |
| `bodies/*.dmf` (7) | **ours**: the characters as shipped before this change (slices 3D-2 to 3D-5; `tools/battle3d/build_assets.sh` and `tools/dialog/build_assets.sh` rebuild Wren's, Brom's, Corwin's and the Rogue's here) | the bodies, skeletons, rest poses and embedded clips of the shipped characters (slice 3D-6 added Corwin's: the Ranger at 1,000 triangles) |

Rebuild everything derived from this folder:

```
python3 tools/vs1art/import_props.py          # textures + props -> romdisk/ (byte-identical: --check)
tools/vs1art/build_chars.sh                   # the 7 characters -> romdisk/models/ (byte-identical)
VS1PY=<py3.12 + tools/vs1art/requirements-vs1.txt> KAYKIT=../KayKit tools/vs1art/build_chars.sh   # + the 4 VS1-recipe heads
```

`tests/vs1art/run.sh` checks all of it (with `VS1PY=` also that the vendored converter reproduces VS1's uploaded
skeleton and Mage byte for byte).
