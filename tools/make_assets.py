#!/usr/bin/env python3
"""
Asset generator (source of truth for placeholder art). Re-run after editing a grid:
    python3 tools/make_assets.py
Outputs
  romdisk/ui_icons.png        128x128 RGBA atlas, 4x4 cells of 32x32 (16x16 art at 2x)
  docs/ui_icons_preview.png   labelled 4x preview sheet (not shipped)
  romdisk/hero.png, guard.png, slime.png  character sheets (128x128, 4x4 cells of 32x32)
  romdisk/tiles.png           tileset (order == enum Tile in src/field.h)
  romdisk/font.png            5x7 bitmap font, ASCII at cell (c%16, c/16)
  docs/slice_art_preview.png  preview of the slice art
Cell order MUST match src/icons.h (enum Icon).
Transparent pixels are RGBA(0,0,0,0). Icons, characters and the font load as ARGB1555 (TR list);
tiles load opaque (RGB565, OP list).
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
    'N': (122, 74, 42, 255),    # leather/wood, hair
    'S': (240, 184, 140, 255),  # skin
    'g': (56, 183, 100, 255),   # green
    'G': (37, 113, 121, 255),   # dark green / teal
    'P': (222, 186, 128, 255),  # path sand
    'p': (186, 146, 96, 255),   # path speckle
    'C': (115, 239, 247, 255),  # cyan (water glint)
    'V': (41, 54, 111, 255),    # deep blue (water)
    't': (30, 86, 60, 255),     # tree canopy shade
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
# B and Y were in the committed atlas but missing here (recovered pixel-exact from romdisk/ui_icons.png)
G_B = ["WWWWW...", "W....W..", "W....W..", "WWWWW...", "W....W..", "W....W..", "WWWWW...", "........"]
G_Y = ["W.....W.", ".W...W..", "..W.W...", "...W....", "...W....", "...W....", "...W....", "........"]
POTION = [   # slice 3D-4: the 4th battle command (GDD 11.1)
    "................",
    "......KKKK......",
    "......KNNK......",
    "......KNNK......",
    ".....KKLLKK.....",
    "......KLLK......",
    ".....KLLLLK.....",
    "....KLWLLLLK....",
    "...KLWRRRRRLK...",
    "...KLRRRRRRRK...",
    "...KRRORRRRRK...",
    "...KRRRRRRRRK...",
    "...KRRRRRRRRK...",
    "....KRRRRRRK....",
    ".....KKKKKK.....",
    "................",
]

ICONS = [   # order == enum Icon in src/icons.h
    ("sword", SWORD), ("flame", FLAME), ("shield", SHIELD), ("target", TARGET),
    ("enemy_turn", ENEMY_TURN), ("spark", SPARK), ("hurt", HURT), ("pad_a", pad_button(G_A, 'R')),
    ("pad_left", pad_button(G_LEFT)), ("pad_right", pad_button(G_RIGHT)), ("pad_up", pad_button(G_UP)),
    ("pad_down", pad_button(G_DOWN)), ("pad_start", pad_button(G_START, 'G')),
    ("pad_b", pad_button(G_B, 'B')), ("pad_y", pad_button(G_Y, 'g')), ("potion", POTION),
]


# ----------------------------------------------------------------------------
# Characters (16x16 art). Sheets are 128x128: 4x4 cells of 32x32 (art at 2x).
# Rows: 0 down, 1 up, 2 left, 3 right. Cols: 0 stand, 1 walk A, 2 walk B, 3 extra.
# ----------------------------------------------------------------------------
HERO_HEAD_DOWN = [
    "................",
    ".....KKKKKK.....",
    "....KNNNNNNK....",
    "...KNNNNNNNNK...",
    "...KNSSSSSSNK...",
    "...KSKSSSSKSK...",
    "...KSSSSSSSSK...",
    "....KSSSSSSK....",
    "...KKbBBBBbKK...",
    "..KSKBBBBBBKSK..",
    "..KSKBBYYBBKSK..",
    "...KKBBBBBBKK...",
]
HERO_HEAD_UP = [
    "................",
    ".....KKKKKK.....",
    "....KNNNNNNK....",
    "...KNNNNNNNNK...",
    "...KNNNNNNNNK...",
    "...KNNNNNNNNK...",
    "...KSNNNNNNSK...",
    "....KSSSSSSK....",
    "...KKBBBBBBKK...",
    "..KSKBBBBBBKSK..",
    "..KSKBBBBBBKSK..",
    "...KKBBBBBBKK...",
]
LEGS_STAND = ["....KDDKKDDK....", "....KDDKKDDK....", "....KNNKKNNK....", "....KKK..KKK...."]
LEGS_A     = ["....KDDKKDDK....", "....KDDKKNNK....", "....KNNK.KKK....", "....KKK........."]
LEGS_B     = ["....KDDKKDDK....", "....KNNKKDDK....", "....KKK.KNNK....", ".........KKK...."]
HERO_SIDE = [   # facing right
    "................",
    "......KKKKK.....",
    ".....KNNNNNK....",
    "....KNNNNNNNK...",
    "....KNNNSSSSK...",
    "....KNNSSSKSK...",
    "....KNSSSSSSK...",
    ".....KSSSSSK....",
    ".....KBBBBK.....",
    "....KBBSSBBK....",
    "....KBBSSBBK....",
    ".....KBBBBK.....",
]
SIDE_LEGS_STAND = [".....KDDDDK.....", ".....KDDDDK.....", ".....KNNNNNK....", ".....KKKKKK....."]
SIDE_LEGS_A     = [".....KDDDDK.....", "....KDDKKDDK....", "...KNNK..KNNK...", "...KKK....KKK..."]

def mirror(grid): return [r[::-1] for r in grid]
def blink(grid):
    g = [list(r) for r in grid]
    for x in (5, 10):
        if g[5][x] == 'K': g[5][x] = 'S'; g[6][x] = 'K'
    return ["".join(r) for r in g]
def recolor(grid, mapping): return ["".join(mapping.get(c, c) for c in r) for r in grid]

def hero_frames():
    down = [HERO_HEAD_DOWN + LEGS_STAND, HERO_HEAD_DOWN + LEGS_A, HERO_HEAD_DOWN + LEGS_B, blink(HERO_HEAD_DOWN + LEGS_STAND)]
    up   = [HERO_HEAD_UP + LEGS_STAND, HERO_HEAD_UP + LEGS_A, HERO_HEAD_UP + LEGS_B, HERO_HEAD_UP + LEGS_STAND]
    right = [HERO_SIDE + SIDE_LEGS_STAND, HERO_SIDE + SIDE_LEGS_A, HERO_SIDE + SIDE_LEGS_STAND, HERO_SIDE + SIDE_LEGS_STAND]
    left = [mirror(f) for f in right]
    return [down, up, left, right]

GUARD_COLORS = {'B': 'R', 'b': 'O', 'N': 'G', 'Y': 'L'}   # red tunic, grey hair, steel buckle

SLIME = [
    "................",
    "................",
    "................",
    "................",
    "......KKKK......",
    "....KKgggGKK....",
    "...KggWggggGK...",
    "..KgWWgggggggK..",
    "..KgWgggggggGK..",
    ".KggggKggKgggGK.",
    ".KggggKggKgggGK.",
    ".KgggggggggggGK.",
    ".KGggggggggggGK.",
    "..KGGgggggggGK..",
    "...KKGGGGGGKK...",
    ".....KKKKKK.....",
]
SLIME_SQUASH = ["................"] * 5 + SLIME[5:11] + SLIME[12:16] + ["................"]
SLIME_SQUASH = SLIME_SQUASH[-16:] if len(SLIME_SQUASH) > 16 else SLIME_SQUASH
SLIME_SQUASH = (["................"] * (16 - len(SLIME_SQUASH))) + SLIME_SQUASH

# ----------------------------------------------------------------------------
# Tiles (16x16 art, 2x). Order MUST match enum Tile in src/field.h.
# ----------------------------------------------------------------------------
import random as _random
def tile_fill(base, speckle, n, seed, pair=False):
    r = _random.Random(seed); g = [[base] * 16 for _ in range(16)]
    for _ in range(n):
        x, y = r.randrange(16), r.randrange(15)
        g[y][x] = speckle
        if pair: g[y + 1][x] = speckle
    return g
def grid_str(g): return ["".join(r) for r in g]

def tile_grass():   return grid_str(tile_fill('g', 'G', 10, 1, pair=True))
def tile_flowers():
    g = tile_fill('g', 'G', 6, 2, pair=True)
    for (x, y, c) in ((3, 3, 'Y'), (11, 5, 'W'), (6, 11, 'R'), (13, 13, 'Y')):
        g[y][x] = c; g[y - 1][x] = c if c != 'Y' else 'W'; g[y][x - 1] = 'W' if c != 'W' else 'Y'
    return grid_str(g)
def tile_path():    return grid_str(tile_fill('P', 'p', 14, 3))
def tile_wall():
    g = [['L'] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            row = y // 4; off = 4 if row % 2 else 0
            if y % 4 == 3: g[y][x] = 'D'                     # mortar line
            elif (x + off) % 8 == 7: g[y][x] = 'D'           # vertical joint
            elif y % 4 == 0: g[y][x] = 'W' if (x + off) % 8 < 6 else 'L'   # brick top highlight
            else: g[y][x] = 'G' if (x + off) % 8 == 6 else 'L'
    return grid_str(g)
def tile_tree():
    g = [list(r) for r in tile_grass()]
    canopy = [
        "....KKKKKKK.....",
        "..KKgggggGGKK...",
        ".KggWggggggGtK..",
        ".KgWggggggGGtK..",
        "KggggggggGGttK..",
        "KgggggggGGGttK..",
        ".KGgggGGGGttK...",
        ".KtGGGGGttttK...",
        "..KKtttttKKK....",
        "....KKNNKK......",
        "......KNNK......",
        "......KNNK......",
        ".....KKNNKK.....",
    ]
    for y, row in enumerate(canopy):
        for x, c in enumerate(row):
            if c != '.': g[y + 1][x + 1 if x < 15 else x] = c
    return grid_str(g)
def tile_water(phase):
    g = [['B'] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            if (y + (x // 4) + phase * 2) % 6 == 0 and (x + phase * 3) % 8 < 4: g[y][x] = 'b'
            if (y * 7 + x * 3 + phase * 5) % 23 == 0: g[y][x] = 'C'
            if (x * 5 + y * 11) % 29 == 0: g[y][x] = 'V'
    return grid_str(g)

TILES = [("grass", tile_grass()), ("flowers", tile_flowers()), ("path", tile_path()), ("wall", tile_wall()),
         ("tree", tile_tree()), ("water", tile_water(0)), ("water2", tile_water(1))]

# ----------------------------------------------------------------------------
# 5x7 bitmap font in an 8x8 grid; atlas 128x128, ASCII c at cell (c%16, c//16). White; tinted at draw.
# ----------------------------------------------------------------------------
FONT = {
 'A': ".###.|#...#|#...#|#####|#...#|#...#|#...#", 'B': "####.|#...#|#...#|####.|#...#|#...#|####.",
 'C': ".###.|#...#|#....|#....|#....|#...#|.###.", 'D': "####.|#...#|#...#|#...#|#...#|#...#|####.",
 'E': "#####|#....|#....|####.|#....|#....|#####", 'F': "#####|#....|#....|####.|#....|#....|#....",
 'G': ".###.|#...#|#....|#.###|#...#|#...#|.####", 'H': "#...#|#...#|#...#|#####|#...#|#...#|#...#",
 'I': ".###.|..#..|..#..|..#..|..#..|..#..|.###.", 'J': "..###|...#.|...#.|...#.|#..#.|#..#.|.##..",
 'K': "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#", 'L': "#....|#....|#....|#....|#....|#....|#####",
 'M': "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#", 'N': "#...#|#...#|##..#|#.#.#|#..##|#...#|#...#",
 'O': ".###.|#...#|#...#|#...#|#...#|#...#|.###.", 'P': "####.|#...#|#...#|####.|#....|#....|#....",
 'Q': ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#", 'R': "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
 'S': ".####|#....|#....|.###.|....#|....#|####.", 'T': "#####|..#..|..#..|..#..|..#..|..#..|..#..",
 'U': "#...#|#...#|#...#|#...#|#...#|#...#|.###.", 'V': "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
 'W': "#...#|#...#|#...#|#.#.#|#.#.#|#.#.#|.#.#.", 'X': "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
 'Y': "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..", 'Z': "#####|....#|...#.|..#..|.#...|#....|#####",
 '0': ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.", '1': "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
 '2': ".###.|#...#|....#|...#.|..#..|.#...|#####", '3': "#####|...#.|..#..|...#.|....#|#...#|.###.",
 '4': "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.", '5': "#####|#....|####.|....#|....#|#...#|.###.",
 '6': "..##.|.#...|#....|####.|#...#|#...#|.###.", '7': "#####|....#|...#.|..#..|.#...|.#...|.#...",
 '8': ".###.|#...#|#...#|.###.|#...#|#...#|.###.", '9': ".###.|#...#|#...#|.####|....#|...#.|.##..",
 '!': "..#..|..#..|..#..|..#..|..#..|.....|..#..", '?': ".###.|#...#|....#|...#.|..#..|.....|..#..",
 '.': ".....|.....|.....|.....|.....|.##..|.##..", ',': ".....|.....|.....|.....|.##..|..#..|.#...",
 ':': ".....|.##..|.##..|.....|.##..|.##..|.....", '-': ".....|.....|.....|#####|.....|.....|.....",
 '/': ".....|....#|...#.|..#..|.#...|#....|.....", "'": "..#..|..#..|.#...|.....|.....|.....|.....",
 '(': "...#.|..#..|.#...|.#...|.#...|..#..|...#.", ')': ".#...|..#..|...#.|...#.|...#.|..#..|.#...",
 '+': ".....|..#..|..#..|#####|..#..|..#..|.....",
}

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

    print(f"ui_icons.png: {len(ICONS)} icons")

    def sheet(rows, path):
        im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
        for r, frames in enumerate(rows):
            for c, g in enumerate(frames):
                im.paste(render(g, 2), (c * 32, r * 32))
        im.save(os.path.join(ROOT, path))
        return im
    hero = hero_frames()
    sheet(hero, "romdisk/hero.png")
    sheet([[recolor(f, GUARD_COLORS) for f in row] for row in hero], "romdisk/guard.png")
    sheet([[SLIME, SLIME_SQUASH]], "romdisk/slime.png")
    tiles = Image.new("RGBA", (128, 128), (0, 0, 0, 255))
    for i, (name, g) in enumerate(TILES):
        tiles.paste(render(g, 2), ((i % 4) * 32, (i // 4) * 32))
    tiles.save(os.path.join(ROOT, "romdisk/tiles.png"))
    font = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    for ch, rows in FONT.items():
        code = ord(ch); cx, cy = (code % 16) * 8, (code // 16) * 8
        for y, row in enumerate(rows.split("|")):
            assert len(row) == 5, (ch, row)
            for x, c in enumerate(row):
                if c == '#': font.putpixel((cx + 1 + x, cy + y), (255, 255, 255, 255))
    font.save(os.path.join(ROOT, "romdisk/font.png"))
    print(f"hero/guard/slime sheets, tiles.png: {len(TILES)} tiles, font.png: {len(FONT)} glyphs")

    # preview of the new art
    pv = Image.new("RGB", (560, 300), (24, 24, 32)); d = ImageDraw.Draw(pv)
    for r, frames in enumerate(hero):
        for c, g in enumerate(frames):
            pv.paste(render(g, 3), (8 + c * 52, 8 + r * 52), render(g, 3))
            pv.paste(render(recolor(g, GUARD_COLORS), 3), (230 + c * 52, 8 + r * 52), render(recolor(g, GUARD_COLORS), 3))
    for i, g in enumerate((SLIME, SLIME_SQUASH)):
        pv.paste(render(g, 3), (450, 8 + i * 52), render(g, 3))
    for i, (name, g) in enumerate(TILES):
        pv.paste(render(g, 2), (8 + i * 40, 220))
    fi = font.crop((0, 16, 128, 64)).resize((384, 144), Image.NEAREST)
    pv.paste(Image.new("RGB", (150, 60), (0, 0, 0)), (400, 120)); pv.paste(fi.crop((0, 0, 150, 60)), (400, 120), fi.crop((0, 0, 150, 60)))
    d.text((8, 268), "hero (4 dirs x stand/walkA/walkB/extra)   guard (palette swap)   slime   tiles: " + ", ".join(n for n, _ in TILES), fill=(220, 220, 220))
    pv.save(os.path.join(ROOT, "docs/slice_art_preview.png"))

if __name__ == "__main__":
    main()
