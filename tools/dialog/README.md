# tools/dialog: the dialog slice's asset and content tools (slice 3D-5)

| Tool | Does |
|---|---|
| `build_assets.sh` | rebuilds every asset of the slice (font, Wren, Brom, the high-detail conversions, the 9 portraits); byte-identical (`BPY=` Blender's Python; the animation library is `tools/battle3d/build_assets.sh`) |
| `make_font.py` | the CC0 TTF -> `romdisk/fonts/dialog.tex` (256x128 ARGB4444: glyphs + UI icons) + `dialog.fnt` + `reports/dialog_font.json` + `docs/dialog/font_preview.png`. Fills the outlines on the font's pixel grid itself (bit-exact); `--scale`, `--spacing ink|mono`, `--ttf` |
| `make_portraits.py` | portraits from high-detail conversions of the models: library poses, three-point light, 2x supersampled, RGB565 `.dtx`; QC sheet `docs/dialog/portraits.png` |
| `lint.py` | the script compiler in Python (`--dump` == the game's `--dump-scripts`) and the GDD 7.4 checks; `--script FILE` lints another file |
| `qc_frames.py` | render QC: plays a route, finds the moments worth a look, renders them into one sheet (`docs/dialog/qc_dialog.jpg`) |
| `reports/` | the converter's and the font tool's reports |
