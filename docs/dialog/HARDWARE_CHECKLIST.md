# Slice 3D-5 hardware / TV checklist (DoD P3), with the VS1 art

> **Updated for the VS1 art reconciliation** ([SLICE_VS1_ART.md](../SLICE_VS1_ART.md)): new boot files (1.1b),
> texture count and VRAM (1.3), the village look (section 5), the frame-time spots (3.1, 3.6) and the playthrough route.

Hardware is the reference (Flycast is known to look worse). Build under DreamSDK: `make clean && make`, then
again with `make clean && make DIAG=1` for the timings. Tick each line; note the TV / cable (composite, S-video,
RGB, VGA) for the legibility items.

## 1. Boot

| # | Check | Expected |
|---|---|---|
| 1.1 | Serial log: the `FOUND:` lines | 17 new ones, all FOUND: `/rd/fonts/dialog.fnt`, `dialog.tex`, `/rd/scripts/village.txt`, `speakers.txt`, `/rd/models/wren.dmf`, `brom.dmf`, 9 x `/rd/portraits/*.dtx` |
| 1.1b | VS1 art: `FOUND:` `/rd/models/house_stone_r0.dmf`, `/rd/models/wagon_r0.dmf`, `/rd/tex/glass.png`; no `dmf: ... rejected` line (the loader validates the new `PRELIT` flag); `field3d: 22 static instances, 13 prelit colour sets, ... 1487 KiB model arena` (DIAG) | all found; the model arena under its 1,792 KiB |
| 1.2 | No `script:` error line; the game starts on the field, not "SCRIPT ERROR" | a `script: village.txt ok: 4 scripts, 91 ops, ...` line (DIAG) |
| 1.3 | `game_init done: 26 textures, ... KiB texture VRAM used` | 26 textures, ~916 KiB (VS1's 128x128 textures, the generated 256x128 ground, the 128x128 character atlases) |

## 2. Legibility and look (GDD 19 risk: text on a CRT / TV)

| # | Check | Expected |
|---|---|---|
| 2.1 | The objective plate (top left): "SPEAK WITH WREN" | crisp 14 px capitals with a shadow; readable from the sofa |
| 2.1b | The inventory under the HP panel: the potion icon and "X0" (grey) at the start, "X3" after Brom, the gold token after Wren's report | legible; the token recognisable as a medallion |
| 2.2 | Talk to Wren: the page text in the box | every letter distinct; the 2 px design pixels even (no shimmering columns: the font is drawn at integer positions with nearest filtering) |
| 2.3 | Box opacity (alpha 0.88) | the scene barely shows through; text contrast high |
| 2.4 | The name plate (purple WREN, rust BROM, blue ROWAN) | colours distinct, the white name readable |
| 2.5 | Portraits (104x104 in a gold frame) | correct colours, **not scrambled** (if they are: `make clean && make PORTRAIT_KOS_TWIDDLE=1`, TRIAGE) |
| 2.6 | A variant change (Wren: worried -> happy on accepting) | a 2-frame blink of the portrait, then the new one; no torn / half-updated portrait |
| 2.7 | Letterbox bars | solid black, full width, slide in and out smoothly |
| 2.8 | The `!` / `...` markers, the emotes (`!` on meeting Wren, the sweat drop at Brom's cork line) | soft edges, hidden by houses in front of them |
| 2.9 | The 'A Talk' prompt | appears over the NPC only when facing it within ~1.3 tiles |
| 2.10 | Fire in the battle ring before talking to Wren | greyed with a lock; A does nothing; after accepting, lit |
| 2.11 | Brom's rest (second talk, "Rest") | fade to black; at stage 3 "Potions refilled." readable on the black; fade back |

## 3. Frame time (DIAG build; the `frames N: worst CPU frame ... us` report every 300 frames)

| # | Where | Budget |
|---|---|---|
| 3.1 | The field's heaviest view: north-west of the road patrol, with Brom and the stone house in view (hero around tile (24,11), [sweep map](../vs1art/sweep_map.png)); also under a south house (see-through, near-plane clipping) | worst CPU frame < 16.7 ms; 60 fps |
| 3.2 | Standing in a dialog with Wren (after the camera settles) | <= the field's worst |
| 3.3 | A portrait change (accepting Wren's request) | no visible hitch |
| 3.4 | `3D: arena peak A/20480 verts, drops 0` | drops 0 everywhere (field peak ~15.8k; the battle's ~17.4k) |
| 3.5 | `heap in use ... (+0 since last report)` | no growth once every dialog has been seen once |
| 3.6 | The road patrol's battle (Warrior, Rogue, Mage skeletons), its overview shot: now the costliest frame of the game (VS1 heads on four characters, ~17.4k vertices) | worst CPU frame < 16.7 ms. If not: `cp assets/vs1_art/bodies/skel_rogue.dmf romdisk/models/` (and the other bodies) restores the pre-VS1 characters exactly, per model |

## 4. The playthrough (about 5 minutes)

Rowan arrives at the west gate. Talk to Wren (on the road by the first house; accept) -> beat the north-east patrol
(two skeletons, past the stone house) -> beat the squad patrolling the road -> talk to Wren (the blessing: HP 30/30)
-> talk to Brom (beside the stone house's door path) twice (potions, then rest + refill) -> the Bone Warden at the
east end of the road -> talk to Wren ("Hollowmere sleeps easy tonight"). Then lose a battle on purpose: GAME OVER,
A -> back at the last checkpoint with full HP.

## 5. The VS1 village look (compare with [village_tour.jpg](../vs1art/village_tour.jpg))

| # | Check | Expected |
|---|---|---|
| 5.1 | The ground (the painted one, [GROUND_ART.md](../GROUND_ART.md)): `FOUND: /rd/maps/village.gnd` at boot | detailed grass in the vines' green with tufts and flower clusters, brown dirt roads with ruts, pebbles and ragged edges, soft shadows NE of every house, crate and boulder; no tile seams, **no scrambled 2x2 blocks or stripes** (if there are: `make clean && make GROUND_NO_VQ=1`, TRIAGE) |
| 5.2 | House windows | the warm lit glass (the `glass` texture), shutters, door steps; roofs with moss |
| 5.3 | The vines on the houses | leaves with cut-out edges (ARGB1555), not black squares |
| 5.4 | Walk behind a house, a boulder, the wagon | it turns into a translucent ghost (fainter the closer it is to the camera) and returns when he leaves; never opaque-black |
| 5.5 | The map edges (the west gate, the east end) | the camera stops; at most a thin strip of the dark outer ground shows at the side |
| 5.6 | Characters: Rowan's crested helm, the skeletons' skulls with cyan glowing eyes, Wren's hat with its buckle, Brom's bear hood with eyes | as in [cast.jpg](../vs1art/cast.jpg); no seam at the neck (head and body come from two builds) |

Report: the serial log of a `DIAG=1` run, photos of 2.2 / 2.5 / 2.10 on the TV, and anything that differed.
