# assets/arena_src - sources of the battle arena's view-culled houses

`house_plaster_r0.dmf` and `house_stone_r0.dmf` are the field houses of slices 3D-2 to 3D-5 (before the VS1 art,
[docs/SLICE_VS1_ART.md](../../docs/SLICE_VS1_ART.md)). The village battle arena places `house_plaster_b` and
`house_stone_b`, which `tools/battle3d/view_cull.py` cut from these (only the triangles a battle camera can see).
Since the field's `romdisk/models/house_*_r0.dmf` are VS1's prelit houses, the sources live here:
`tools/battle3d/build_assets.sh` (`VIEW_CULL=1`) reads them, and `tests/battle3d/run.sh` renders the arena with
them to check the culled houses draw the same pixels.
