# Planning prototypes for slice 3D-4 (not part of the build)

These scripts produced the images and measurements in `docs/NEXT_SLICE_3D_BATTLE.md` §2. They use
the sandbox paths of this session (`/workspace/KayKit`, `/tmp/bproto`); adjust the constants at the
top before re-running.

| Script | What it does |
|---|---|
| `build.sh` | converts Knight, Skeleton_Warrior and Mage with the candidate battle clips into throwaway DMFs (`--no-hidden-pass`; the native-clip path still assumes looping clips, so one-shots lose their last frame) |
| `sheet.py` | contact sheet per character: 4 frames per clip, weapons attached to `handslot.r/l` -> `../clips_*.jpg` |
| `sheet_lib.py` | shared helpers: posing a DMF, bone world matrices, weapon attachment |
| `arena.py` | arena mock-up from the village kit + boulders with posed characters, ring/arrow placeholders -> `../arena_mock.jpg`; prints the props' triangle counts |
| `impact.py` | impact/release timings (weapon-tip speed peak, lowest point) and hip travel per clip |
