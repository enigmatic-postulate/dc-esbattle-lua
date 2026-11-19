# assets/fonts: source fonts (not shipped on the romdisk)

| File | Font | Licence | Used for |
|---|---|---|---|
| `HomeVideo-Regular.ttf` | "Home Video" 0.80 by GGBotNet (https://ggbot.net) | **CC0 1.0 Universal** (embedded in the font's name table: IDs 13/14, "Creative Commons Zero v1.0 Universal", https://creativecommons.org/publicdomain/zero/1.0/); see `HomeVideo-LICENSE.txt` | the dialog font (decision D1 of `docs/NEXT_SLICE_DIALOG.md`), rasterised by `tools/dialog/make_font.py` into `romdisk/fonts/dialog.tex` + `dialog.fnt` |
| `Poppins-Bold.ttf` | Poppins Bold 4.004, (c) 2020 The Poppins Project Authors (https://github.com/itfoundry/Poppins) | **SIL Open Font License 1.1** (embedded in the font's name table: IDs 13/14); see `Poppins-OFL.txt` | the UI font (battle HUD, damage numbers, prompts, banners, field HUD; `docs/UI_ART.md`), rasterised with anti-aliasing and a baked outline by `tools/ui/make_ui_atlas.py` into `romdisk/ui/ui.uia` |

Only the rasterised atlas ships (`romdisk/fonts/`); the TTF is the source the tool reads. The font is
a pixel font (a 100-unit design grid, 600-unit monospaced advance, capitals only: its lower-case
code points carry the capital shapes). The tool renders it at exactly 2x its design grid (20 px em:
10x14 px capitals, 2 px design pixels) without anti-aliasing; see `docs/SLICE_DIALOG.md` §3.
