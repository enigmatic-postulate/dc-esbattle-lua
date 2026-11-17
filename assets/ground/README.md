# assets/ground - inputs of the painted village ground

`T_Noise_Terrain.png`: the Quaternius Medieval Village MegaKit's terrain noise (`Village/Textures/`, CC0 1.0,
by @Quaternius; the kit's licence: `../../Village/License_Quaternius.txt` in the upload). 512x512, tileable, three
independent noise channels. `tools/ground/paint_ground.py` uses it for the grass's large patches and the dirt's
variation, so the ground's variation comes from the same kit as the houses. Nothing else is read from here: the
rest of the ground is painted from the map and the props (docs/GROUND_ART.md).
