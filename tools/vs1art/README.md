# tools/vs1art - the VS1 art into this runtime

[docs/SLICE_VS1_ART.md](../../docs/SLICE_VS1_ART.md) explains what was adopted and why. Inputs are in
[assets/vs1_art/](../../assets/vs1_art/README.md).

| Tool | What | Needs |
|---|---|---|
| `import_props.py` | VS1's 10 textures -> `romdisk/tex/*.png` (untwiddled, RGB565 exact); VS1's 13 prop variants -> our DMF v2 (`PRELIT`, un-rotated, one material per submesh, double-sided). `--check`: convert in memory, compare with `romdisk/` | python3, numpy, pillow |
| `import_chars.py` | a VS1 character -> our DMF (`convert`); VS1's head parts on one of our bodies (`--body`, what ships); `--retexture` | python3, numpy, pillow |
| `build_chars.sh` | the six shipped characters (`OUT=dir` to write elsewhere); with `VS1PY=` also the three VS1-recipe heads VS1 did not ship | python3; `VS1PY`: Python >= 3.12 with `requirements-vs1.txt` (assimp-py, ufbx, meshoptimizer) |
| `sweep_map.py` | frame cost over every walkable tile (`battle_trace --sweep`) as a heat map + summary | python3, pillow |
| `vs1_fbx2dmf.py`, `dmf_format.py`, `pvrtex.py`, `raster.py` | **vendored unchanged** from VS1 (`larp3-2/tools/dmf/`): its FBX converter, its DMF reader/writer, its PVR texture helpers | see above |

The vendored VS1 converter reproduces VS1's uploaded `skeleton.dmf` and `mage.dmf` byte for byte
(`VS1PY=... tests/vs1art/run.sh`). Install for it:

```
uv venv -p 3.12 .venv-vs1 && uv pip install --python .venv-vs1/bin/python -r tools/vs1art/requirements-vs1.txt
```
