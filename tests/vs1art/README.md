# tests/vs1art - the VS1 art reconciliation

`tests/vs1art/run.sh` (14 checks with `VS1PY=`, 12 without; ~4 minutes). What each proves: `docs/SLICE_VS1_ART.md` §7.

| File | Checks |
|---|---|
| `run.sh` | the suite |
| `check_props.py` | every triangle of the 13 converted props lands where VS1 put it (drawn as the field draws it), with VS1's UVs, baked colours, textures, winding; the 9 RGB565 textures survive the PNG round trip bit for bit; the vine's ARGB4444 -> ARGB1555 |
| `check_chars.py` | the character converter keeps every VS1 triangle; each shipped character = VS1's head triangles + our body's other triangles, our skeleton and clips, VS1's texture, contiguous cyan eyes, and the posed head matches VS1's posed head |
| `check_layout.py` | the village's tiles and props == VS1's map (footprints converted; the one documented move), and the GDD's positions |
| `check_ground.py` | the generated ground (maps without a painted one: `test_field`) == an independent transcription of VS1's generator, bit for bit |
| `check_gnd.py` | the painted village ground (docs/GROUND_ART.md): the file, the game's VQ decode == an independent one, and the art targets against the VS1 ground (detail, saturation, contrast, hue distance to the kit's colours) |
| `check_camera.py` | every field frame of a route uses VS1's camera (eye = look + (0, 9, 9), clamped to the map) |

Also used: `battle_trace --sweep` (frame cost per tile; `tools/vs1art/sweep_map.py`), `--tune near_strips=0` (the
former near-plane path, for the pixel A/B) and `tests/battle3d/compare_dumps.py`.
