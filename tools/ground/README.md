# tools/ground - the painted village ground

`paint_ground.py` paints `romdisk/maps/<map>.gnd` from the map file (tiles and props), the prop models in
`romdisk/models/` (for their shadows) and the kit's terrain noise (`assets/ground/`), then VQ-compresses it.
What it paints and why: [docs/GROUND_ART.md](../../docs/GROUND_ART.md).

```
python3 tools/ground/paint_ground.py                       # romdisk/maps/village.txt -> romdisk/maps/village.gnd (~13 s)
python3 tools/ground/paint_ground.py --raw r.png --preview p.png    # the painting before / after VQ
python3 tools/ground/paint_ground.py --check               # rebuild in memory, compare byte for byte
```

Re-run it after moving, adding or removing a prop, or editing the map's tiles: the shadows, the paths and the
flower clusters come from them. A map without a `.gnd` keeps the generated VS1 ground (`field3d.cpp`).
Needs python3, numpy, pillow. `tests/vs1art/check_gnd.py` checks the result in the game.
