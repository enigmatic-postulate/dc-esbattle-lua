#!/usr/bin/env python3
"""
Asset generator (source of truth for placeholder art). Re-run after editing a grid:
    python3 tools/make_assets.py
Outputs
  romdisk/ui_icons.png        128x128 RGBA atlas, 4x4 cells of 32x32 (16x16 art at 2x)
  docs/ui_icons_preview.png   labelled 4x preview sheet (not shipped)
  romdisk/es_sprite_64.png    64x64 (lossless 1/16 of the former 1024x1024 16x upscale), if still 1024
Cell order MUST match src/icons.h (enum Icon).
Transparent pixels are RGBA(0,0,0,0): black when loaded opaque (PNG_NO_ALPHA, as today),
transparent once the engine loads with alpha (planned, see docs/NEXT_SLICE.md).
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Sweetie-16-derived palette
PAL = {
    '.': (0, 0, 0, 0),
    'K': (26, 28, 44, 255),     # outline
    'W': (244, 244, 244, 255),
    'L': (148, 176, 194, 255),  # light steel
    'G': (86, 108, 134, 255),
    'D': (51, 60, 87, 255),
    'Y': (255, 205, 117, 255),
    'O': (239, 125, 87, 255),
    'R': (177, 62, 83, 255),
    'B': (59, 93, 201, 255),
    'b': (65, 166, 246, 255),
    'N': (122, 74, 42, 255),    # leather/wood
}

SWORD = [
    "............KKKK",
    "...........KWWLK",
    "..........KWWLK.",
    ".........KWWLK..",
    "........KWWLK...",
    ".......KWWLK....",
    "..K...KWWLK.....",
    ".KYK.KWWLK......",
    ".KYYKWWLK.......",
    "..KYYWLK........",
    "...KYYK.........",
    "..KNKYYK........",
    ".KNK.KYK........",
    "KNK...K.........",
    "KK..............",
    "................",
]
FLAME = [
    "......K.........",
    ".....KOK........",
    ".....KOOK.......",
    "....KOYOK..K....",
    "...KOYYOK.KOK...",
    "...KOYYYOKOOK...",
    "..KOYYWYYOOYOK..",
    "..KOYWWWYYYYOK..",
    ".KOYYWWWWYYYOK..",
    ".KOYWWWWWWYYOK..",
    ".KOYWWWWWWYYOK..",
    ".KROYWWWWYYORK..",
    "..KROYYYYYORK...",
    "...KRROOORRK....",
    "....KKKKKKK.....",
    "................",
]
SHIELD = [
    "................",
    ".KKKKKKKKKKKKKK.",
    ".KYYYYYYYYYYYYK.",
    ".KYBBBBWWBBBBYK.",
    ".KYBBBBWWBBBBYK.",
    ".KYBBBBWWBBBBYK.",
    ".KYWWWWWWWWWWYK.",
    ".KYBBBBWWBBBBYK.",
    ".KYBBBBWWBBBBYK.",
    "..KYBBBWWBBBYK..",
    "..KYBBBWWBBBYK..",
    "...KYBBWWBBYK...",
    "....KYBWWBYK....",
    ".....KYBBYK.....",
    "......KYYK......",
    ".......KK.......",
]
TARGET = [   # player target cursor: down arrow
    "................",
    "....KKKKKKKK....",
    "....KWYYYYOK....",
    "....KWYYYYOK....",
    "....KWYYYYOK....",
    "....KWYYYYOK....",
    ".KKKKWYYYYOKKKK.",
    ".KWWWYYYYYYOOOK.",
    "..KWYYYYYYYYOK..",
    "...KWYYYYYYOK...",
    "....KWYYYYOK....",
    ".....KWYYOK.....",
    "......KYOK......",
    ".......KK.......",
    "................",
    "................",
]
ENEMY_TURN = [   # acting-enemy marker: red double chevron
    "................",
    "..KKK......KKK..",
    "..KRRK....KRRK..",
    "...KRRK..KRRK...",
    "....KRRKKRRK....",
    ".....KRRRRK.....",
    "......KRRK......",
    ".......KK.......",
    "..KKK......KKK..",
    "..KOOK....KOOK..",
    "...KOOK..KOOK...",
    "....KOOKKOOK....",
    ".....KOOOOK.....",
    "......KOOK......",
    ".......KK.......",
    "................",
]
SPARK = [   # hit flash
    ".......K........",
    "......KWK.......",
    ".K....KWK....K..",
    ".KWK..KYK..KWK..",
    "..KWKKYYYKKWK...",
    "...KYYYWYYYK....",
    "..KKYYWWWYYKK...",
    "KWWYYWWWWWYYWWK.",
    "..KKYYWWWYYKK...",
    "...KYYYWYYYK....",
    "..KWKKYYYKKWK...",
    ".KWK..KYK..KWK..",
    ".K....KWK....K..",
    "......KWK.......",
    ".......K........",
    "................",
]
HURT = [row.replace('W', 'O').replace('Y', 'R') for row in SPARK]   # red burst (palette swap)

def pad_button(glyph, face='D'):
    base = ["..KKKKKKKKKKKK..", ".K" + face * 12 + "K."] + ["K" + face * 14 + "K"] * 12 + \
           [".K" + face * 12 + "K.", "..KKKKKKKKKKKK.."]
    rows = [list(r) for r in base]
    for y, g in enumerate(glyph):
        for x, ch in enumerate(g):
            if ch != '.': rows[4 + y][4 + x] = ch
    return ["".join(r) for r in rows]

G_A = ["...WW...", "..W..W..", ".W....W.", ".W....W.", ".WWWWWW.", ".W....W.", ".W....W.", "........"]
G_UP = ["...WW...", "..WWWW..", ".WWWWWW.", "WWWWWWWW", "...WW...", "...WW...", "...WW...", "........"]
G_DOWN = G_UP[:7][::-1] + ["........"]
G_LEFT = ["".join(G_UP[7 - 1 - x][y] if x < 7 else '.' for x in range(8)) for y in range(8)]
G_LEFT = ["".join(G_UP[x][y] for x in range(7)) + "." for y in range(8)]          # transpose: arrow points left
G_RIGHT = [r[:7][::-1] + "." for r in G_LEFT]
G_START = ["..WWWW..", ".W....W.", ".W......", "..WWWW..", "......W.", ".W....W.", "..WWWW..", "........"]

ICONS = [   # order == enum Icon in src/icons.h
    ("sword", SWORD), ("flame", FLAME), ("shield", SHIELD), ("target", TARGET),
    ("enemy_turn", ENEMY_TURN), ("spark", SPARK), ("hurt", HURT), ("pad_a", pad_button(G_A, 'R')),
    ("pad_left", pad_button(G_LEFT)), ("pad_right", pad_button(G_RIGHT)), ("pad_up", pad_button(G_UP)),
    ("pad_down", pad_button(G_DOWN)), ("pad_start", pad_button(G_START, 'G')),
]

def render(grid, scale):
    assert len(grid) == 16 and all(len(r) == 16 for r in grid), [len(r) for r in grid]
    im = Image.new("RGBA", (16, 16))
    im.putdata([PAL[c] for r in grid for c in r])
    return im.resize((16 * scale, 16 * scale), Image.NEAREST)

def main():
    atlas = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    for i, (name, grid) in enumerate(ICONS):
        atlas.paste(render(grid, 2), ((i % 4) * 32, (i // 4) * 32))
    atlas.save(os.path.join(ROOT, "romdisk/ui_icons.png"))

    # preview sheet: each icon at 4x on dark + at 1x/2x "in-game" sizes on black
    cols = 5; cw, ch = 150, 110
    sheet = Image.new("RGB", (cols * cw, ((len(ICONS) + cols - 1) // cols) * ch), (24, 24, 32))
    d = ImageDraw.Draw(sheet)
    for i, (name, grid) in enumerate(ICONS):
        x, y = (i % cols) * cw, (i // cols) * ch
        sheet.paste(Image.new("RGB", (64, 64), (0, 0, 0)), (x + 8, y + 8))
        sheet.paste(render(grid, 4), (x + 8, y + 8), render(grid, 4))
        sheet.paste(Image.new("RGB", (48, 48), (0, 0, 0)), (x + 84, y + 8))
        sheet.paste(render(grid, 1), (x + 86, y + 10), render(grid, 1))
        sheet.paste(render(grid, 2), (x + 98, y + 22), render(grid, 2))
        d.text((x + 8, y + 80), f"{i}: {name}", fill=(220, 220, 220))
    sheet.save(os.path.join(ROOT, "docs/ui_icons_preview.png"))

    sp = os.path.join(ROOT, "romdisk/es_sprite_64.png")
    src = Image.open(sp)
    if src.size == (1024, 1024):
        small = src.resize((64, 64), Image.NEAREST)   # exact: source is a 16x nearest upscale
        small.info = {}
        small.save(sp, transparency=src.info.get("transparency"))
        print("es_sprite_64.png: 1024x1024 -> 64x64")
    print(f"ui_icons.png: {len(ICONS)} icons")

main()
