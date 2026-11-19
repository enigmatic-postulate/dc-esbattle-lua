# Slice 3D-6 hardware / TV checklist (the title, the gate, the barrow, the ending)

Slices 3D-5, the VS1 art, the VQ ground and this slice have **not run on hardware** (docs/SLICE_BARROW.md §6): run
[../dialog/HARDWARE_CHECKLIST.md](../dialog/HARDWARE_CHECKLIST.md) first, then this. Build under DreamSDK:
`make clean && make`, and `make clean && make DIAG=1` for the timings. Note the TV and cable for the legibility items.

## 1. Boot and the title

| # | Check | Expected |
|---|---|---|
| 1.1 | Serial log: `FOUND:` for the slice's files (TRIAGE "Slice 3D-6"); `script: 3 files ok: 12 scripts, ...`; `game_boot: title` | all found, no `MISSING`, no `script:` error |
| 1.2 | `game_init done` / the DIAG texture report | 28 textures, ~980 KiB texture VRAM |
| 1.3 | The title: the village at dusk (warm, orange-brown, long light) turning slowly; the logo fades in; PRESS START blinks | the logo's pixel letters crisp at 2x, the gold-to-ember gradient and the lilac subtitle readable, the drop shadow soft |
| 1.4 | Start -> the menu (NEW GAME / CREDITS) grows; up/down; B back | the gold frame and the arrow; a held button never selects on the frame the menu appears |
| 1.5 | Leave the pad for 40 s | fade to the DEMO: Rowan talks to Wren, fights the north-east patrol, talks to Brom; any button returns to the title |
| 1.6 | CREDITS from the menu | the roll over the village, Start skips, back to the title |

## 2. A new game to the gate

| # | Check | Expected |
|---|---|---|
| 2.1 | NEW GAME: black, "Hollowmere. / The night the barrow woke." (A skips), the intro | Rowan walks in under the arch; Wren's `!`; the camera eases back; "Speak with Wren" |
| 2.2 | Start during the intro | the intro ends at once with the same state (Rowan on (3,15), the objective) |
| 2.3 | Before Wren's token, walk east on the road to its end | Corwin steps onto the road, Rowan is walked back a tile, two lines; again on every try |
| 2.4 | After the report: Corwin's `!`; talk to him | "Wren's token. Well then." ...; he steps aside; "The east road is open."; a gold glow at the road's end |
| 2.5 | T5 into every battle | two white flashes, then a black diamond closing from the corners; the battle fades in |

## 3. The barrow (GDD 12.5)

| # | Check | Expected |
|---|---|---|
| 3.1 | Walk into the glow: T7 | black, then the barrow at night fading in while Rowan walks in from the west; no visible hitch before the black |
| 3.2 | The night look | blue night light, the painted ground (dead grass, the stone path), withered trees, iron fences, tombs, the crypt, the purple rune ring; **fog**: the far side of the barrow fades into dark blue (if it is all grey or absent: `NO_FOG=1`, TRIAGE) |
| 3.3 | The ground | no garbled blocks (else `GROUND_NO_VQ=1`) |
| 3.4 | The waystone (`!`, A) | "An old waystone..."; HP full; "Lay the Warden to rest"; the ring glows brighter |
| 3.5 | Walk back out by the west path | the village at the road's end, daylight again; and back in |
| 3.6 | Step into the ring | letterbox, the crypt, the crane to the Warden rising (1.5x), THE BONE WARDEN, three lines, T5, the battle's intro without a second name card |
| 3.7 | Lose to him (guard only) | T9: the scene slows, tints grey-red with a dark vignette, YOU HAVE FALLEN; options after ~1.7 s, input after ~2.7 s; the tip about raising the shield; RETRY: back at the waystone, full HP, the short intro (about a second), the battle |

## 4. The ending

| # | Check | Expected |
|---|---|---|
| 4.1 | Beat the Warden | VICTORY! with no "PRESS A"; after ~2.5 s a white flash and white |
| 4.2 | Dawn | from white into the barrow at dawn: the light warms over 4 s, the fog lifts; "...Thank you, knight." |
| 4.3 | The village in the morning | Wren, Brom and Corwin by the wagon, Rowan walks up, three lines, everyone jumps; the crane up; THE END with the logo |
| 4.4 | Credits, results | the roll (Start skips); the card: play time, battles, turns, damage, potions, retries, the rank; A -> black -> the title with the gold CLEARED crest |

## 5. Frame time (DIAG: `worst CPU frame ... us` every 300 frames; the host's estimates in SLICE_BARROW.md §5)

| # | Where | Budget |
|---|---|---|
| 5.1 | The title's orbit (a whole minute) | < 16.7 ms, 60 fps |
| 5.2 | The barrow (walk everywhere) | < 16.7 ms |
| 5.3 | The Warden's crane and the boss battle | < 16.7 ms |
| 5.4 | The dawn lerp (characters lit per frame) and the village gathering (four characters) | < 16.7 ms |
| 5.5 | The credits | < 16.7 ms |
| 5.6 | `3D: arena peak A/20480 verts, drops D` | drops 0 everywhere |
| 5.7 | `heap in use ... (+0 since last report)` | no growth after the first visit of every screen |

Report: the DIAG serial log of one playthrough, photos of 1.3, 3.2, 3.7 and 4.2, and anything that differed.
