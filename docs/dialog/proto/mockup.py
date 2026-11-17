#!/usr/bin/env python3
"""Dialog slice plan mock-up: GDD 7.1 layout drawn over a real host-rendered village frame.
Left: field (objective HUD, gold '!' marker, 'A Talk' prompt). Right: dialog (letterbox, 2-shot
push-in standing in for the T3 camera, box 592x118 at (24,318) alpha 0.88, 104x104 portrait in a
2 px gold frame, name plate, 3 text lines, advance marker, a choice box).
Stand-ins: DejaVu Sans for the CC0 font (decision D1); the Skeleton_Mage field model for Wren.

    python3 docs/dialog/proto/mockup.py field.png portraits_dir out.jpg
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FONTB = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
GOLD = (232, 184, 72)


def tr_rect(img, box, rgb, a):
    ov = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(ov).rectangle(box, fill=rgb + (int(255 * a),))
    return Image.alpha_composite(img, ov)


def field_panel(frame, marker_xy, hero_xy):
    im = frame.convert('RGBA')
    im = tr_rect(im, (16, 14, 250, 40), (0, 0, 0), 0.55)
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(FONT, 14)
    d.text((24, 18), 'Speak with Wren', font=f, fill=(255, 240, 200))
    mx, my = marker_xy                                        # gold '!' billboard (bobbing)
    d.ellipse((mx - 13, my - 13, mx + 13, my + 13), fill=GOLD, outline=(90, 60, 10), width=2)
    d.text((mx - 3, my - 11), '!', font=ImageFont.truetype(FONTB, 19), fill=(60, 30, 0))
    hx, hy = hero_xy                                          # 'A Talk' prompt
    im = tr_rect(im, (hx - 36, hy - 12, hx + 40, hy + 12), (0, 0, 0), 0.6)
    d = ImageDraw.Draw(im)
    d.ellipse((hx - 32, hy - 9, hx - 14, hy + 9), fill=(200, 60, 60))
    d.text((hx - 27, hy - 9), 'A', font=ImageFont.truetype(FONTB, 13), fill=(255, 255, 255))
    d.text((hx - 8, hy - 9), 'Talk', font=f, fill=(255, 255, 255))
    return im


def dialog_panel(frame, focus, portrait, lines, name, name_rgb, choices=None):
    fx, fy = focus
    z = 1.35                                                   # stand-in for the dialog camera push-in
    w, h = 640 / z, 480 / z
    x0, y0 = max(0, min(640 - w, fx - w / 2)), max(0, min(480 - h, fy - h * 0.34))
    im = frame.crop((int(x0), int(y0), int(x0 + w), int(y0 + h))).resize((640, 480), Image.BILINEAR).convert('RGBA')
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 640, 36), fill=(0, 0, 0)); d.rectangle((0, 444, 640, 480), fill=(0, 0, 0))
    im = tr_rect(im, (24, 318, 24 + 592, 318 + 118), (16, 14, 28), 0.88)
    d = ImageDraw.Draw(im)
    d.rectangle((24, 318, 24 + 591, 318 + 117), outline=(120, 100, 70), width=1)
    px, py = 31, 325                                           # portrait 104x104 in a 2 px gold frame
    d.rectangle((px - 2, py - 2, px + 105, py + 105), fill=GOLD)
    im.paste(portrait.resize((104, 104), Image.BILINEAR), (px, py))
    tx = px + 104 + 14
    im = tr_rect(im, (tx - 6, 300, tx + 90, 322), name_rgb, 0.95)   # name plate
    d = ImageDraw.Draw(im)
    d.text((tx + 2, 302), name, font=ImageFont.truetype(FONTB, 14), fill=(255, 255, 255))
    f = ImageFont.truetype(FONT, 15)
    for k, ln in enumerate(lines):
        d.text((tx, 334 + k * 22), ln, font=f, fill=(244, 244, 244))
    d.polygon([(596, 418), (606, 418), (601, 425)], fill=GOLD)      # advance marker
    if choices:
        cw, ch = 236, 14 + 26 * len(choices)
        cx, cy = 616 - cw, 318 - 24 - ch
        im = tr_rect(im, (cx, cy, cx + cw, cy + ch), (16, 14, 28), 0.88)
        d = ImageDraw.Draw(im)
        d.rectangle((cx, cy, cx + cw - 1, cy + ch - 1), outline=(120, 100, 70))
        for k, c in enumerate(choices):
            d.text((cx + 28, cy + 8 + 26 * k), c, font=f, fill=(255, 224, 150) if k == 0 else (220, 220, 220))
        d.polygon([(cx + 12, cy + 12), (cx + 12, cy + 24), (cx + 20, cy + 18)], fill=GOLD)
    return im


def main():
    frame = Image.open(sys.argv[1]).convert('RGB')
    pdir, out = sys.argv[2], sys.argv[3]
    a = field_panel(frame, (254, 150), (318, 300))
    b = dialog_panel(frame, (285, 240), Image.open(pdir + '/wren_worried.png'),
                     ['The dead are walking again.', 'Two patrols came over the hill last night,', 'and the old barrow gate is humming.'],
                     'Wren', (96, 56, 150), ['I will deal with them.', 'Tell me more.', 'Not now.'])
    s = Image.new('RGB', (1290, 480), (0, 0, 0))
    s.paste(a.convert('RGB'), (0, 0)); s.paste(b.convert('RGB'), (650, 0))
    s.save(out, quality=90)
    print('wrote', out)


if __name__ == '__main__':
    main()
