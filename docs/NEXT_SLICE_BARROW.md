# Next vertical slice (3D-6): "Into the Barrow" - the night half, the ending, and the frame around the game

> **Status:** implemented - see [SLICE_BARROW.md](SLICE_BARROW.md) (what was built, measured and decided). The text below is the plan as written. Builds on slice 3D-5 ("Talk to Wren", `docs/SLICE_DIALOG.md`), the
> VS1 art reconciliation (`docs/SLICE_VS1_ART.md`) and the ground art pass (`docs/GROUND_ART.md`). GDD:
> `docs/GDD_Horizontal_Slice.pdf` (v1.0); "GDD 12.5" refers to its sections. Decisions marked **Recommendation**
> are open until you confirm them (§13).

**Goal:** finish the GDD's horizontal slice. After this slice a new player can go from the **title screen**, through
the village half that exists today, past **Corwin's gate**, into **the Barrow** at night, beat **the Bone Warden** in his
own staging, watch **the dawn ending** and **the credits**, see a **results card**, and land back on the title - at 60
fps on a Dreamcast, with every mode change a catalogued transition and every loss a **death screen** with a retry.

> Rowan walks out of the dusk title into Hollowmere. The quest runs as today until Wren hands him her token. At the
> east road, **Corwin** stops him ("Road's closed"), then reads the token and steps aside; the road's end glows. A fade,
> and Rowan walks into **the Barrow**: night, fog, tombstones, a crypt. He touches an old **waystone** (checkpoint),
> steps into the rune ring, and the camera **cranes** from the crypt to the Warden rising: **THE BONE WARDEN**. The
> battle is slice 3D-4's. If he falls, the screen slows, tints grey-red, **YOU HAVE FALLEN**, and he retries from the
> waystone with the short intro. If he wins: a white flash, **dawn** over the barrow, "Let me sleep now", and the
> village in morning light, Wren, Brom and Corwin cheering by the wagon. **THE END**, credits, a results card, title.

In GDD milestone terms this is **H1 (flow skeleton) + the rest of H4 (triggers, exits, Corwin) + H5 (the barrow and the
boss's staging; the boss's rules exist) + H6 (title, death, ending)**, with H7's hardware pass. It completes GDD 1.1
except audio, VMU saving, the pause menu and the extra art assets, which stay optional (§1.3).

This document has 14 parts:
* §1: scope and GDD traceability;
* §2: starting point (what the code does today, and the gaps that shape the plan);
* §3: findings (measured for this plan);
* §4: player flow;
* §5: design;
* §6: architecture;
* §7: data formats;
* §8: content;
* §9: budgets;
* §10: stages and tasks;
* §11: tests;
* §12: definition of done;
* §13: decisions and risks;
* §14: pointers for the implementers.

---

## 1. Scope and GDD traceability

### 1.1 In scope

| # | Item | GDD | Notes |
|---|---|---|---|
| S1 | **Game flow**: modes Boot, Title, Attract, Intro/Cutscene, Field, Dialog, ToBattle, Battle, ToField, MapChange, Death, Ending, Credits, Results; one entry point for every mode change | 3, 15.1 | replaces `GameMode`'s Field/ToBattle/Battle/ToField/GameOver/Error/Dialog |
| S2 | **Transition catalogue T1-T11** (data: id, ticks, visual), input locked while one runs, press-edge after each | 6, 4 | T3/T4 exist (slice 3D-5); T6 exists as today's fade; the rest is new, incl. T5's white flashes + diamond iris and T9's slow-motion tint |
| S3 | **Map triggers and exits**, **conditional triggers/exits** (`if`, `once`), **map change T7** with a 30-tick walk-in, per-map state | 7.2, 15.2, 6 | the village's east exit; the barrow's west exit back (§5.3, Recommendation D4) |
| S4 | **Corwin** (KayKit Ranger): model (VS1 head on our body, as the cast), 3 portraits, `corwin_block` trigger, `corwin_main`, the gate, S3 -> S4 | 9.3, App. A | undoes slice 3D-5's AD3/AD4 (the Warden on the east road; rewritten Corwin lines) |
| S5 | **Cutscene commands** in the script VM: `move`, `cam`, `cmd \| cmd` (parallel), `battle`, `map`, `light`, `namecard`, `card`, `transition`, `credits`, `results`, `title`; **Start skips** a cutscene (not a choice) | 7.4 | the compiler rejects them today ("not in this slice") |
| S6 | **The Barrow** (24x16, night): map, procedural graveyard props (A1 stand-ins exist), painted ground in a night palette, **light presets** (day, dusk, night, dawn, morning) and `light lerp`, **PVR table fog**, the waystone (C3, S5), the boss ring trigger, the **field boss intro T8** (crane, name card, 3 lines) and the 60-tick retry version | 12.5, 6, App. A | the Warden leaves the village; his battle (rules, AI, phases, arena, in-battle intro) exists from slice 3D-4 |
| S7 | **Death screen** (T9: 0.5x for 30 t, grey-red tint + vignette, YOU HAVE FALLEN, RETRY FROM CHECKPOINT / RETURN TO TITLE, input after 60 t, context tips) | 13 | replaces GAME OVER; retry already restores the checkpoint exactly |
| S8 | **Ending** (E1-E7): VICTORY without "press A", T10, dawn lerp + fog lifting, the wisp's line, the village in morning light, the gathering (Wren, Brom, Corwin), the cheer, crane up, THE END, credits roll (Start skips), results card (time, battles, turns, damage, potions, retries, rank), T11, the cleared crest | 14, App. A | one script, `ending` |
| S9 | **Title** (village at dusk, slow orbit, logo, PRESS START, NEW GAME / CREDITS, cleared crest), **boot T1**, **T2** text card, **intro cutscene**; **attract demo** after 40 s idle | 5, 6, App. A | the logo is typeset from the dialog font (A3 stand-in) |
| S10 | **Portraits**: Corwin, the Warden (3 variants each) | 8 | the portrait tool exists |
| S11 | **Session stats** for the results card; quest **stages S4, S5, S6, S7** as the GDD numbers them | 10, 14, App. B | slice 3D-5's AD6 (stage 6 = the end) is undone |
| S12 | **Audio hooks** (no audio): named events emitted at the GDD's SFX/music moments, logged in traces | 16.3 (R1, R2) | so the separate audio track attaches without touching game code (§13, D11) |

### 1.2 Adaptations this plan proposes

| # | GDD says | Plan | Why |
|---|---|---|---|
| BA1 | The waystone is a `barrow_stone` script (trigger or talk unspecified) | an **interactable object**: an `npc` with a static look (`stone`), `talk barrow_stone`, marker `!` until used | the talk prompt, markers and dialog camera already exist; a trigger would fire without the player's intent |
| BA2 | Ending: "Wren, Brom and Corwin gather by the wagon" | the gathering stays **by the wagon**, which the VS1 art moved to (10,6) | measured (§3, finding 1): the road centre would cost ~19-20k vertices with four characters; the wagon's surroundings ~16k |
| BA3 | The village map has `exit east 39 15 ...` only | the barrow also gets **an exit back** (west edge -> the village's east road), closed while the ending runs | GDD pillar 3 ("no dead ends"): the player may want Brom's rest before the boss |
| BA4 | Boss intro: a crane in the field, then the battle | the field T8 carries the crane, name card and the 3 lines; the **battle's own intro runs its short form** (no second name card) | the battle already has an intro with a name card (slice 3D-4); two in a row would repeat |
| BA5 | Attract mode plays "a recorded input script" | inputs recorded with the host harness (`--record-inputs`) from a story route, RLE-packed on the romdisk, fed through the input layer | the game is deterministic (float == double, integer field logic); the same inputs give the same demo on hardware |

### 1.3 Out of scope (optional after the definition of done, in this order)

| Item | GDD | Why not now |
|---|---|---|
| Audio (music, SFX) | 16.3 R1/R2, G5 | no AICA/streaming path exists: a separate 2-3 week track (GDD 18). This slice emits the events (S12) |
| VMU save / CONTINUE | 15.4, G4 | GDD stretch; the Progress struct is ready for it (64 bytes) |
| Pause menu | 4 (Start in the field) | GDD stretch |
| Boot studio splash | 3 | optional in the GDD; the boot goes straight to T1 |
| Final art: graveyard set (A1), logo (A3), dialog frame (A4), cast/cheer clips (A5), wisp flipbook (A6), sky strips (A7), face textures (R3) | 16.2, 16.3 | every one has a stand-in that drops out without code changes |
| A skeleton "battle bark" portrait | 8 | nothing in the scripts uses it |

---

## 2. Starting point

What exists (tests green: `tests/vs1art/run.sh` 14/14, `tests/dialog/run.sh` 35/35, `tests/battle3d/run.sh` 21/21,
`tests/run_tests.sh` 54/54):

| System | Today | Gap for this slice |
|---|---|---|
| Modes | `GameMode { Field, ToBattle, Battle, ToField, GameOver, Error, Dialog }` in `src/game.cpp`; fades are a counter (`game_fade_alpha`) | S1, S2: a table-driven flow; most modes are new |
| Map | one map per session; `field_load()` resets `FieldState`; entities, props, `if` conditions; `spawn` | no triggers, exits, lights, shots; `Progress.defeated` is 16 bits **of the current map's entities** |
| Scripts | compiled once at boot **against the current map** (`script.cpp` resolves actor names to `field_state()` entity indices) | a script that crosses maps (`ending`) or runs on the barrow needs per-map binding (§5.4) |
| VM | pages, choices, conditions, flags, `face`, `anim`, `emote`, `wait`, `fade`, `letterbox` | no `move`, `cam`, parallel lines, `battle`, `map`, `light`, cards, ending commands |
| Story driver (`platform/host/story.cpp`) | `talk`, `chase`, `goto`, `choose`, `wait`, `press`, `expect`, ... on one map | map changes, cutscene skipping, title/menu steps, the death screen |
| Field renderer | VS1 camera, painted VQ ground (one per session: no `texture_free`), PRELIT props, see-through, two-shot | light presets, tint, fog, scripted cameras, per-look scale (the Warden is 1.5x) |
| Battle | 3D battle, boss AI/phases/summons, barrow arena (night light), in-battle intro + name card, GAME OVER | the death screen; VICTORY without "press A" for the boss; hand-over to the ending script |
| Checkpoints | RAM, `Progress` copy, exact restore (tested) | map id in the checkpoint; C3 on the barrow; the "boss retry" route |
| Portraits | 2 VRAM slots, 9-file RAM cache (`PORTRAIT_CACHE`) | +6 files (Corwin, Warden) |
| Budgets | field worst 15,761 written vertices / 1.42 M (estimate); battle 17,434 / 1.57 M; VRAM 916 KiB; romdisk 2.45 MB of 2.5 | new scenes: title orbit, intro, barrow, T8 crane, ending gathering, credits flyover |
| Hardware | slice 3D-4 ran on hardware and Flycast. **Slice 3D-5, the VS1 art and the VQ ground have never run on hardware** | Stage 0 (§10) |

---

## 3. Findings (measured for this plan)

1. **Ending gathering cost.** From the field sweep (`battle_trace --sweep`, hero on every tile, boot entities):
   written vertices around the wagon (8-13, 7-10) are median 6,647 / max 8,646 with one character in view; the road
   centre (17-23, 15-16) median 9,857 / max 13,852 with up to 4. A shipped character writes 3,053-3,500 vertices
   (Knight 3,500; Wren 3,376; Brom 3,053). Scenery alone is ~3k by the wagon and ~6k on the road centre, so four
   characters by the wagon come to **~16k**, on the road **~19-20k** (over the 18k budget). -> BA2.
2. **Scripts bind to one map.** `script.cpp` resolves `wren`, `brom`, ... to entity indices of the map loaded at boot
   (lines 131, 480-532, 696, 741, 868). The `ending` script uses actors on two maps, the barrow's scripts only exist
   there. -> §5.4.
3. **`Progress.defeated` is per-map.** 16 bits indexed by the current map's entity order; a second map's entities would
   alias them. -> §5.3 (map id + per-map bitsets).
4. **Texture and memory headroom** (from `--resources` and the romdisk size): VRAM 916 of 1,024 KiB budget (hardware
   has ~4.4 MiB free after frame buffers); texture slots 26 of 32 (`MAX_SPRITES`); materials 145 of 256; model arena
   1,487 of 1,792 KiB; romdisk 2,447,274 of 2,500,000 bytes; portrait cache 9 files. Projected additions in §9: the
   romdisk and the portrait cache must grow; VRAM stays under 1 MiB only if the barrow's ground **reuses** the
   village's VQ texture slot (same 512x512 size).
5. **The boss is ready.** Rules, AI rotation, phases, summons, balance (good 95 %, naive 48 %), the barrow arena, the
   in-battle intro and name card, the phase cutscene are slice 3D-4's and tested. This slice only moves where and how
   the fight starts and what happens after it.
6. **The barrow fits the painted ground.** 24x16 + a 1-tile margin at 12 texels per tile is 312x216 texels: the same
   512x512 VQ layout as the village (`tools/ground/paint_ground.py` needs a palette per map).
7. **Existing stand-ins for the graveyard**: `tomb_round`, `tomb_cross`, `crypt` (procedural, `tools/battle3d/
   make_props.py`), the kit tree tinted dark; a waystone and a rune-ring decal are new procedural props (S).

---

## 4. Player flow

```
Boot -T1 45t-> Title (dusk orbit, logo fades in, PRESS START blinks)
   idle 40 s -> Attract (DEMO) -any button-> Title
   NEW GAME -T2 (30 + 15 + card 90 + 30)-> Intro cutscene (Rowan walks in, Wren waves '!', objective)
   CREDITS -> Credits roll -> Title
Field (village, as slice 3D-5) ... S3 token from Wren
   east road: trigger road_block (if !gate_open) -> corwin_block: Corwin steps in, Rowan walked back one tile
   Corwin (has_token): "Wren's token. Well then." -> gate_open, S4, objective "Enter the Barrow", exit glow
   exit east (if gate_open) -T7 (20 + 20, walk-in 30)-> Barrow (night, fog)
Field (barrow)
   waystone (A): heal, checkpoint C3, S5, objective "Lay the Warden to rest"
   boss ring trigger -> warden_intro (T8: crane, name card, 3 lines; 60 t on retries) -T5-> boss battle
      lose -T9-> Death: RETRY (-> waystone, then ring = short intro) | RETURN TO TITLE (-T11-> Title)
      win  -> E1 (collapse, wisp, VICTORY, no press) -T10-> E2 dawn lerp in the barrow, the wisp's line
           -> map village (morning) E3 gathering by the wagon, 3 lines, cheer
           -> E4 crane up, THE END -> E5 credits (Start skips) -> E6 results (A) -T11-> Title (cleared crest)
Any battle lost (patrols included) -T9-> Death (tips by context)
```

Stages (GDD App. B): S3 "Show the token to Corwin" (Corwin `!`), S4 "Enter the Barrow" (exit glow), S5 "Lay the Warden
to rest" (ring glow, C3), S6 Warden beaten (the ending runs), S7 complete (title crest).

---

## 5. Design

### 5.1 Flow and transitions (`src/game_flow.*`, replacing the mode switch in `game.cpp`)

* One function starts every mode change: `flow_go(Mode to, TransitionId t)`. It locks input for the transition's
  ticks, runs its visual, swaps the mode at the transition's midpoint (fades) or end (wipes), and requires a fresh
  press afterwards (GDD 4). The catalogue is a `const` table `{id, ticks..., visual}`; traces print `N <tick> <mode>`
  and a new `X` line per transition start/end, so tests assert exact ticks (GDD 6: "the harness asserts the exact
  tick each transition starts and ends").
* Visuals, all from full-screen translucent quads drawn last (GDD 6 notes): fade black/white; letterbox (exists);
  **white flash** (T5, T10); **diamond iris** (T5: a 32-segment ring polygon, opaque, closing); **tint** (T9, and the
  light presets) = the global tint of §5.6 + a translucent colour overlay + a vignette (a ring of quads with alpha
  increasing outwards); **slow motion** (T9) scales presentation time only (battle3d's sequence clock), never rules.
* T5 replaces today's 24-tick fade into battles (8 + 24 t, then the battle intro); T6 stays.

### 5.2 Triggers and exits (`field.cpp`)

* `trigger <id> <x> <y> <w> <h> <script> [once] [if <cond>]`: a tile rectangle; the hero's feet box entering it (edge,
  not level) starts the script (Dialog/Cutscene mode). `once` is remembered in Progress (a flag bit per trigger, or a
  `@flags` entry; Recommendation: auto-declared flags `trig_<id>`).
* `exit <id> <x> <y> <w> <h> <map> <tx> <ty> <dir> [if <cond>]`: entering it starts T7 to `<map>` with the hero on
  `(tx, ty)` facing `dir`, plus 30 ticks of scripted walk-in (the hero walks one tile in `dir`). While `if` is false
  the exit tiles **block** like map edges (the road's end is closed, not a hole).
* Both are evaluated after the hero moves, in the same tick order as NPC contact, deterministic (integer boxes).
* The **road block** (Corwin): `corwin_block` walks the hero back one tile (`move rowan -1 0`): the trigger must not
  re-fire while the script runs and only re-arms when the hero has left the rectangle.

### 5.3 Maps, Progress and checkpoints

* `Progress` gains `map` (index into a small romdisk table `maps.txt`: `village`, `barrow`) and per-map defeated bits
  (`defeated[2]` or one 32-bit set with per-map offsets). It must stay <= 64 bytes (GDD 10.1). Checkpoints copy it
  whole, so a retry reloads the checkpoint's map when it differs (T9's retry fade covers the load).
* Loading a map: `field_load(path)` as today; entity visibility/defeated from Progress; the field3d scene rebuild;
  the **painted ground re-uploaded into the same VQ slot** (`eng::texture_update_vq`, new: same 512x512 size; the
  previous map is not drawn after the fade-out, which satisfies the "2 frames" rule of `texture_update`).
* Models: the model arena is append-only (no free). Everything both maps need is loaded once; the barrow adds only
  small procedural props (§9).
* The Warden: a field entity on the barrow (`look guard`, scale 1.5 via a per-look scale, §5.7), **dormant** until the
  intro raises him (`anim warden skel_awaken`); the boss battle is started by the script (`battle warden boss`), not by
  contact. The village's east-road Warden is removed (undoes AD3).

### 5.4 Scripts across maps (`script.cpp`, `tools/dialog/lint.py`)

* **Recommendation (D2):** every map's scripts are compiled at boot into the one pool (village.txt, barrow.txt,
  ending.txt); each script declares the map it runs on (`@map barrow`; `ending` declares none and may only use actors
  through `map` commands). Actor names resolve **per map at map load** into a small table (script actor slot -> entity
  index), so the compiled ops keep actor *slots*. The lint checks every actor against the map(s) the script can run
  on; the C++ == Python dump parity stays the contract.
* `ending` runs across the map change: the VM must survive `map` (it blocks until T7 finished and the new map is
  bound).

### 5.5 Cutscene commands (the VM; GDD 7.4)

| Command | Semantics | Notes |
|---|---|---|
| `move <actor> <x> <y> [run]` / `move <actor> <dx> <dy>` (relative, as `move rowan -1 0`) | walks in a straight line at walk (or run) speed, blocks until arrival; entities step tiles, the hero moves in subpixels; collisions ignored during a cutscene | the hero's "walked back one tile" and the walk-in reuse it |
| `cam <shot> [ticks]` | eases (smoothstep) to a named shot from the map file (`shot <id> eye x y z look x y z fov`), 0 = cut; `cam field [ticks]` returns to the follow camera | the two-shot stays automatic for talk scripts |
| `a \| b \| ...` | runs the actor commands in parallel; the line ends when all have ended | only `move`, `anim`, `face`, `emote`, `cam`, `wait` may be parallel (lint) |
| `battle <encounter> [boss]` | T5 into the battle; the script continues after a win (a loss goes to Death) | the ending's `goto ending` after the boss |
| `map <name> <x> <y> <dir> [light <preset>]` | T7 to the map; blocks until it is bound | §5.4 |
| `light <preset>` / `light lerp <a> <b> <ticks>` | §5.6 | presets are data (`lights.txt`) |
| `namecard "text"`, `card "text"` | the boss name card (as the battle's), a centred full-screen title card (T2's text card, THE END) | dialog font |
| `transition Tn` | plays a catalogue transition inside a script | T10 in `ending` |
| `credits`, `results`, `title` | the ending's modes (blocking until the player leaves them) | `title` ends the session |

* **Skip (Start):** fast-forwards the running cutscene to its next choice or its end: the VM runs tick by tick with the
  presentation off (no rendering, waits and moves resolve at once), so every state change happens exactly as when
  watched (**Recommendation D7**; deterministic, and the full-playthrough test can compare watched vs skipped end
  states). Choices are never skipped (GDD 4). The intro, T8 (after the first time), the ending and the credits
  are skippable; talk scripts are not (they have pages: A already speeds them).

### 5.6 Light presets, tint and fog (`field3d.cpp`, `render3d.cpp`, KOS backend)

* A preset: light direction, ambient, diffuse, light colour (characters), **tint** (multiplies prelit props, the
  ground and billboards), clear colour, fog colour + start/end (or none). Presets: `day` (today's values), `dusk`
  (title), `night` (barrow: the arena's 0.55/0.60/0.95, ambient 0.35), `dawn`, `morning` (ending).
* Characters bake their lighting per yaw at init (`bake_look`, 16 buckets): a preset change re-bakes (~100k
  instructions per look); a **lerp** re-bakes at most every 8 ticks, spread over frames like battle3d's
  `bake_slice` (512 vertices per frame).
* Prelit props (VS1) take the tint only: their colour sets (`g_color_pool`) are re-tinted in place when the tint
  changes (~17k colours x ~10 instructions), in steps during a lerp - **never per vertex per frame**.
* The ground's vertex colour (white today) becomes the tint; the painted ground is drawn as is otherwise.
* **PVR table fog** on the barrow: `pvr_fog_table_color` + `pvr_fog_table_linear`/`exp2` at map load, and every
  material compiled with `PVR_FOG_TABLE` when the map has fog (a material flag; materials are per model, so the
  barrow's props and characters need their fogged variants - count them, §9). The host renderer
  (`tools/render_frame.py`) must emulate it (W-based table lookup) or the QC images lie. **Kill switch:**
  `make NO_FOG=1` (TRIAGE), and the night preset must look acceptable without fog.

### 5.7 Characters and looks

* **Corwin**: `Ranger.fbx`: body with `tools/battle3d/convert_model.py` (~1,000 triangles, like Wren), head with VS1's
  recipe (`tools/vs1art/vs1_fbx2dmf.py --max-error 0.02 --part-error <the Ranger's head part>=0.005`; the converter
  lists the part names), then `import_chars.py --body`), 128x128; expected ~2,000 vertices / ~3,000 written. Look `watch` (library clips: `idle_a`, `walk`,
  `interact`). Portraits (3 variants) with `tools/dialog/make_portraits.py` from a high-detail conversion.
* **The Warden in the field**: the `guard` look (skel_mage) at **1.5x**: add a per-look scale to `LOOKS[]` (today one
  `ENEMY_SCALE`); the marker/prompt heights follow `top`. Rising: `skel_awaken` (library) as a field overlay clip.
* **The waystone**: a static look (no skeleton) - LOOKS entries need a "static" kind, or the waystone is a prop plus
  an invisible npc (Recommendation: a static look; the talk prompt, marker and two-shot work unchanged).
* `LIT_POOL_VERTS` and `PORTRAIT_CACHE` grow (§9).

### 5.8 Death screen (`death.cpp`)

* Enter from the battle's loss (all battles). T9: the killing blow's sequence completes (battle3d already holds the
  end), 0.5x for 30 t with the hero's `death_a` held and enemies `skel_taunt`, camera push-in (the existing defeat
  push-in), tint to grey-red over 40 t + vignette, YOU HAVE FALLEN over 30 t, options at tick 100, input from tick 60
  of the options (GDD 13: "input accepted after 60 ticks").
* Tips: one line by context: the boss -> "When the Warden gathers the dark, raise your shield"; a patrol -> "Fireball
  burns every enemy at once"; potions unused (`potions == 3` at the loss) -> "Potions restore 12 HP".
* RETRY: `checkpoint_restore()` (exists), map reload if needed, fade 30 + 20 t into the field; if the loss was the
  boss, run `warden_intro` at once in its short form. RETURN TO TITLE: T11, the session discarded (stats too).

### 5.9 Ending, credits, results (`ending.cpp` + the `ending` script)

* E1: the boss battle's win runs its defeat staging and a VICTORY banner **without** "press A" (a flag on the boss
  encounter), then returns to the `ending` script instead of the field.
* E2-E4: script (App. A) with this plan's additions: `light lerp night dawn 240` + fog fading out; `map village ...
  light morning`; the gathering by the wagon (BA2); `anim all cheer` = `jump_short` (A5 stand-in) on each; `cam
  crane_up 180`; `card "THE END"` + the logo.
* E5 credits: a data file (`romdisk/scripts/credits.txt`: headings and lines) scrolled over a slow flyover of the
  village (a camera path of shots), Start skips.
* E6 results: `SessionStats` (not checkpointed: play ticks, battles won, hero turns, damage dealt/taken, potions used,
  retries) -> the card; rank (Recommendation D8): Gold = under 20 min and 0 retries, Silver = under 30 min and <= 2
  retries, Bronze otherwise; A -> T11.
* E7: `Progress`-independent `cleared` flag for the session (the title crest).

### 5.10 Title, intro, attract (`title.cpp`)

* Backdrop: the village map loaded with the `dusk` preset, the camera on a slow eased orbit (60 s per loop) around a
  point chosen by the frame-cost sweep of the orbit path (§11, T3), chimney smoke particles (battle3d's particle
  system) optional. No entities (or the boot entities idle; Recommendation: none, cheaper and no stale quest state).
* Logo: "PROJECT HYMN" / "The Restless Dead" typeset with the dialog font into a 256x64 ARGB4444 texture by a tool
  (A3 stand-in; a real logo replaces the file), 2x with a soft shadow, fades in over 45 t after T1.
* PRESS START blinks (30-t period); Start or A opens the menu (12-t grow): NEW GAME, CREDITS (CONTINUE greyed only
  if VMU is ever done). Cleared crest under the logo after a clear this session.
* NEW GAME: T2 (fade 30, hold 15, text card "Hollowmere. The night the barrow woke." 90 t, skippable with A, fade in
  30) -> the `intro` script on the village (App. A: Rowan walks in from the gate, Wren's `!` and wave, the objective).
  It replaces slice 3D-5's `new_game` stand-in.
* Attract: after 40 s without input, fade to the demo: the boot state of a new game, inputs from
  `romdisk/demo/attract.in` (RLE) fed to `input.cpp` instead of the pad; "DEMO" label; any button -> T1 to the title.
  The demo must end before any randomness the inputs do not pin down (the battle bot's choices are inputs too).

### 5.11 Audio hooks (S12)

`audio_event(AudioEvent id)`: a no-op on KOS and an `A <tick> <name>` trace line on the host, called at: text blip
(per glyph batch), menu move/confirm/cancel, footsteps (locomotion phase), swing, hit, fireball, explosion, shield,
bones collapse, potion, gate open, and music cues (title, village, barrow, battle, boss, victory, death, credits). The
audio track later implements it; the traces already prove when each would play.

---

## 6. Architecture

```
game_flow (new)   modes + transition catalogue; owns the session (SessionStats, cleared); replaces game.cpp's switch
  ├─ title (new)        backdrop (field3d with dusk + orbit cam), logo, menu, attract feeder
  ├─ field / field3d    + triggers, exits, light presets, tint, fog, cutscene camera, per-look scale, texture_update_vq
  ├─ script VM          + cutscene ops, parallel lines, skip, per-map actor binding
  ├─ dialog_ui          + name card, title cards, credits roll, results card (2D, dialog font)
  ├─ battle / battle3d  + "no press" boss victory, hand-over to the ending, slow-motion hook for T9
  ├─ death (new)        T9 presentation + menu + retry routing
  └─ progress           + map id, per-map defeated bits, trigger flags; SessionStats (separate, not checkpointed)
engine (KOS / host)     + texture_update_vq, fog (table + per-material flag), tint constants
tools                   + paint_ground palettes per map, make_logo.py, make_portraits (Corwin, Warden), Corwin build,
                          make_props (waystone, rune ring), lint (new commands, @map, credits.txt)
```

Rules for every new module (as slices 3D-4/3D-5): fixed pools, no heap after boot, integer/tick logic, float ==
double traces, everything data-driven where the GDD says "data".

---

## 7. Data formats

### 7.1 Village map additions (`romdisk/maps/village.txt`)

```
entity corwin npc watch 37 13 left talk corwin_main
trigger road_block 38 14 2 4 corwin_block if !gate_open
exit    east       39 15 1 2 barrow 4 7 right if gate_open
light   day
shot    arrival    ...     # intro: eye x y z, look x y z, fov
shot    crane_up   ...     # ending
# removed: entity warden ... (moves to the barrow)
```

`prop` lines are unchanged; **re-run `tools/ground/paint_ground.py`** after any tile or prop edit (its `--check` fails
otherwise), and `tests/vs1art/check_layout.py` pins the VS1 props and the GDD positions (add Corwin's (37,13)).

### 7.2 The Barrow (`romdisk/maps/barrow.txt`, proposal to be tuned with renders and the sweep)

`T` = dead trees (the kit tree under the night tint; 111 vertices each, ~25 in view: measure with the sweep), `#` = iron fence (the fence model), `=` = the old path from the
west exit (0,7) to the rune ring, `,` = pale flowers. The crypt stands north of the ring, tombstones around both.

```
name barrow
map 24 16
TTTTTTTTTTTTTTTTTTTTTTTT
T......,.....T.........T
T......................T
T.....................#T
T.....................#T
T.....................#T
T....,.................T
=================......T
=================......T
T......................T
T.....................#T
T....,................#T
T......................T
T......................T
T.......,..............T
TTTTTTTTTTTTTTTTTTTTTTTT
spawn 1 7 right                      # only used by the --map harness; arrivals come through the exit
light night
fog   0B1022 12 30                   # colour, start, end (units from the eye)
prop crypt      19.5  3.5   0 solid 5 3       # tiles 17-21 x 2-4, north of the ring
prop tomb_round 11.5  4.5  10 solid 1 1       # ... a dozen, around the ring and along the path
prop rune_ring  18.0  7.5   0                 # decal, non-blocking (new procedural prop)
entity waystone npc stone 10 6 down talk barrow_stone
entity warden   enemy guard 18 6 down warden asleep       # new keyword: drawn lying down, untouchable; risen by the intro,
                                                          # fought through the script
trigger ring    16 6 5 4 warden_intro if stage >= 5 && !warden_down
exit    west    0 7 1 2 village 38 15 left if !warden_down
shot    crypt   ...
shot    warden_rise ...
shot    barrow_dawn ...
```

### 7.3 Script additions

New files: `romdisk/scripts/barrow.txt` (`@map barrow`: `barrow_stone`, `warden_intro`, `warden_phase2` if the battle
keeps calling it), `ending.txt` (`ending`), `intro` in `village.txt`, `credits.txt` (plain lines: `# heading`, text,
blank = gap). Speakers: `corwin` (name plate colour of the Ranger's green), `warden` (purple). Flags: `gate_open`,
`warden_seen` (short intro), trigger `once` flags.

### 7.4 Lights (`romdisk/maps/lights.txt`)

`light <name> dir x y z ambient a diffuse d colour rrggbb tint rrggbb clear rrggbb [fog rrggbb start end]`.

---

## 8. Content

* Scripts: GDD Appendix A verbatim where the systems now exist: `intro`, `corwin_block`, `corwin_main`,
  `barrow_stone`, `warden_intro`, `ending`; `wren_main`'s stage 3-5 line back to the GDD's ("Corwin is waiting at the
  east road."); the objectives of App. B for S3-S5; `after_battle` no longer ends the game at the Warden (S6 is the
  ending's). Every deviation marked `# slice 3D-6` with its reason, as slice 3D-5 did.
* Undo list from slice 3D-5 (`romdisk/scripts/village.txt` header): **AD3** (the Warden on the east road), **AD4**
  (Corwin lines rewritten), **AD6** (stage 6 = the end), **AD7** (GAME OVER); AD1 is already undone (VS1 map).
* Credits (GDD E5): team roles, tools (KallistiOS, DreamSDK, SH4ZAM), asset credits: KayKit by Kay Lousberg (CC0),
  Medieval Village MegaKit by Quaternius (CC0), Poly Haven (CC0), the "Home Video" font by GGBotNet (CC0), and the VS1
  lineage's art pipeline.
* Painted grounds: the barrow's palette (dead olive grass, grey-brown earth, pale stone path, moss), night-tinted at
  runtime (`light night`); the village's stays.

---

## 9. Budgets (projected from today's measured numbers)

| Resource | Today | Adds | Projected | Limit (proposed) |
|---|---:|---|---:|---:|
| Field frame, written vertices (worst tile) | 15,761 | the ending gathering (4 characters, BA2), title orbit, T8 crane, credits flyover | <= 18,000 each, **measured by path sweeps** (T3) | 18,000, 0 drops (unchanged) |
| SH4 estimate (worst) | 1.42 M field, 1.57 M battle | same scenes | <= 1.60 M | 1.60 M - **recalibrate with the Stage 0 hardware numbers** |
| Texture slots | 26 / 32 | Corwin 1, logo 1; barrow ground reuses the VQ slot | 28 | 30 |
| VRAM | 916 KiB | Corwin 32, logo 32 (256x64 ARGB4444) | ~980 KiB | 1 MiB (1.25 MiB if fog variants or a sky strip need it) |
| PVR materials | 145 / 256 | Corwin 2-4, waystone/ring ~4, fog variants of the barrow's set (props + characters ~30) | ~185 | 200 |
| Model arena | 1,487 / 1,792 KiB | Corwin ~105 KiB, waystone + ring ~10 KiB | ~1,600 KiB | 1,700 of 1,792 (raise the arena to 2 MiB only if needed) |
| Romdisk (in RAM) | 2.45 MB | Corwin ~105 KB, 6 portraits ~197 KB, barrow map + `.gnd` ~70 KB, scripts/credits ~20 KB, logo ~10 KB, attract inputs < 5 KB | **~2.86 MB** | **3.0 MB** (add total RAM to `--resources`: bss + romdisk + arenas <= 12 MiB of 16) |
| Portrait cache (RAM) | 9 files, 288 KiB | 6 | 15 files, 480 KiB | - |
| NPC/enemy lighting pool (`LIT_POOL_VERTS`) | 7,680 x 16 | Corwin ~2,000, the waystone (static: none) | 9,728 x 16 (622 KiB) | - |
| Script pools | 23,232 B static | barrow, ending, intro, ~60 ops + ~3 KiB text; credits outside the pool | ~27 KiB | 32 KiB |
| Heap after boot | 0 | - | 0 | 0 |

---

## 10. Stages and tasks

Sizes: S <= 1 day, M 2-3 days, L about a week (one developer who knows the code).

### Stage 0: the hardware gate and safety nets (first; nothing else depends on guesses)
| # | Task | Size |
|---|---|---|
| 0.1 | **Commit/tag the current tree** (everything since the baseline commit is uncommitted) | S |
| 0.2 | DreamSDK build; run `docs/dialog/HARDWARE_CHECKLIST.md` (slice 3D-5 + VS1 art + VQ ground: portraits' twiddle, VQ ground, PRELIT props, vines, see-through) on hardware and Flycast; `DIAG=1` logs at the field's hot spot (24,11), the road squad's battle overview, a dialog | - (you) |
| 0.3 | Recalibrate `tests/dialog/sh4_model.py` with the DIAG worst CPU frames (replace the 3D-4 qemu calibration); re-state the 18k / 1.60 M limits from measured time. If a scene is over 16.7 ms: the levers in TRIAGE (bodies back per model) before new content | S |
| 0.4 | `--resources` gains total RAM (bss + romdisk + arenas) and the romdisk size; the budget checks read it | S |

### Stage A: flow and transitions (the skeleton first, with placeholders, as GDD H1)
| # | Task | Size |
|---|---|---|
| A1 | `game_flow`: modes, `flow_go`, input lock + press-edge, `N`/`X` trace lines; placeholder Title/Death/Ending/Credits/Results screens | M |
| A2 | Transition visuals: fades (black/white), white flash, diamond iris, tint overlay + vignette, slow-motion hook; the catalogue table T1-T11 | M |
| A3 | `SessionStats`; Death replaces GameOver (retry/title routing, tips) with the final T9 presentation | M |
| A4 | Story driver: title/menu steps, `skip`, death-screen choices, map-aware `goto`/`chase`; the **full playthrough route** runs title -> results -> title on placeholders (exit check of this stage) | M |

### Stage B: maps
| # | Task | Size |
|---|---|---|
| B1 | Map file: `trigger`, `exit`, `light`, `shot`, `fog`; loader errors with line numbers; lint of cross-references | M |
| B2 | Progress: map id, per-map defeated bits, trigger `once` flags; checkpoint across maps; exit T7 + walk-in | M |
| B3 | Scripts across maps: `@map`, actor slots bound at map load; lint + dump parity | M |
| B4 | `eng::texture_update_vq` (host + KOS); the second painted ground in the same slot; `paint_ground.py` palettes per map | S |

### Stage C: cutscene commands
| # | Task | Size |
|---|---|---|
| C1 | `move` (hero + entities), `cam` (shots, eases, cut, back to field), parallel lines | M |
| C2 | `battle`, `map`, `light`, `namecard`, `card`, `transition`, `credits`, `results`, `title`; Start = skip (fast-forward) | M |
| C3 | lint.py: every new command, `@map`, parallel rules, the credits file; negative fixtures for each | S |

### Stage D: Corwin and the gate
| # | Task | Size |
|---|---|---|
| D1 | Corwin: body + VS1 head + 128x128, `qc_model`, portraits x 3; `check_chars.py` CAST entry | S |
| D2 | Look `watch`; `corwin_block` trigger (re-arm rule), `corwin_main`, `gate_open`, S4 + exit glow marker; undo AD3/AD4 | M |

### Stage E: the Barrow and the boss's staging
| # | Task | Size |
|---|---|---|
| E1 | Barrow map, props (+ waystone, rune ring in `make_props.py`), painted ground, dead trees | M |
| E2 | Light presets + lerp (re-bake in slices, re-tint colour pools in steps), tint on ground and billboards | M |
| E3 | PVR table fog: KOS (`pvr_fog_table_*`, per-material flag), host emulation in `render_frame.py`, `NO_FOG=1` | M |
| E4 | Waystone (static look, C3, S5), ring trigger, `warden_intro` (T8 crane, name card, 3 lines, short retry form, BA4), the dormant Warden at 1.5x rising, Warden portraits x 3 | M |
| E5 | Boss hand-over: VICTORY without press, return to the `ending` script; death -> short intro retry | S |

### Stage F: the ending and the title
| # | Task | Size |
|---|---|---|
| F1 | `ending` script end to end: T10, dawn lerp + fog out, the wisp line, village morning, gathering, cheer, crane, THE END | M |
| F2 | Credits roll (data file, flyover path, Start skips), results card (stats, rank), T11, cleared crest | M |
| F3 | Title: dusk orbit backdrop, `make_logo.py`, PRESS START, menu, T1/T2, the `intro` cutscene | M |
| F4 | Attract demo: recorded inputs (RLE), input feeder, DEMO label, exit on any button (**should**) | S |
| F5 | T5 (flash + iris) into every battle | S |
| F6 | Audio hooks (S12) | S |

### Stage G: verification, hardware, docs
| # | Task | Size |
|---|---|---|
| G1 | `tests/barrow/run.sh` (§11); the other four suites green | M |
| G2 | Path sweeps (title orbit, intro, T8 crane, ending, credits flyover) + the barrow tile sweep; QC image sheets (title, T2 card, Corwin, barrow, T8, death, dawn, gathering, THE END, credits, results) | S |
| G3 | `docs/SLICE_BARROW.md`; HOWTO (a map exit/trigger, a cutscene, a light preset, a second map's ground); TRIAGE (fog, VQ slot reuse, map change, skip); hardware checklist | S |
| G4 | Hardware pass: fades/iris/tint, fog, the light lerp, title and credits legibility on a TV, DIAG at the title orbit, the T8 crane, the gathering, the boss battle | - (you) |

**Order:** 0 -> A1-A4 (the whole game title-to-title on placeholders) -> B1-B4 -> C1-C3 -> D1-D2 -> E1-E5 -> F5, F1-F3,
F6 -> G; F4 last. The first demo inside the slice: the village east exit into a grey-box barrow and back, with T7,
before any art.

**Rough total:** 5-6 weeks for one developer (GDD 18 sizes H1 M + H4 remainder M + H5 L + H6 M + H7 M, minus the boss
rules that exist, plus Stage 0 and the cross-map script binding the GDD did not foresee).

---

## 11. Tests (`tests/barrow/run.sh`, in the style of the existing suites)

| # | Check | How |
|---|---|---|
| T1 | **Full playthrough**: title -> T2 -> intro -> quest -> Corwin -> barrow -> waystone -> T8 -> boss -> ending -> credits -> results -> T11 -> title (crest shown) with no soft lock; the mode and stage sequence and **every transition's start and end tick** asserted | story route `full.route` + `--bot-battles smart`; float == double |
| T2 | Order variations: slice 3D-5's routes extended to the end (patrols first, skipping Brom, everyone twice incl. Corwin before and after the token, the barrow exit back to the village and in again) end in the same final state | routes |
| T3 | **Frame budget on every new camera**: the barrow tile sweep; path sweeps sampling the title orbit, the intro shots, the T8 crane, the ending gathering and the credits flyover every few ticks (a `--sweep-path` option: a script or camera path instead of hero tiles) | <= 18,000 written, 0 drops, <= 1.60 M |
| T4 | Death and retry at every checkpoint (C0-C3, Brom's rest) and in every battle: Progress == snapshot byte for byte, the map and tile, full HP; the boss retry runs the short intro (60 t) | forced losses |
| T5 | Triggers/exits: `once`, `if`, re-arm after leaving; the road block walks back exactly one tile and cannot be walked through before `gate_open`; closed exits block | unit routes + `--probe` style grid |
| T6 | Cutscenes: watched == skipped end state (Progress, entity positions, camera mode) for intro, T8, ending; Start never skips a choice | routes with and without `skip` |
| T7 | Scripts: lint clean; C++ dump == Python dump (cross-map slots, new ops); negative fixtures (unknown shot, actor not on the script's map, parallel non-actor command, missing credits file, `battle` without encounter) | `lint.py`, `check_lint_fixtures.sh` |
| T8 | Light and fog: a preset change re-bakes within its tick budget (no frame over budget during a lerp); the host fog emulation matches a Python reference; tint applied to props/ground/billboards | trace counters + render QC |
| T9 | Transitions visuals: T5 iris geometry (32 segments, closes in 24 t), T9 tint/vignette/slow-motion ticks, T10 white flash, cards' timing | `X` lines + frame dumps |
| T10 | Results: stats equal counters recomputed from the trace (battles, turns, damage, potions, retries, ticks); rank thresholds | `check_results.py` |
| T11 | Attract: 40 s idle starts it, any button returns, the demo replays identically twice (trace equality) | idle input file |
| T12 | Resources: slots, VRAM, materials, arena, romdisk, total RAM, portrait cache, pools within §9; 0 heap allocations over the full playthrough | `--resources`, `battle_trace_alloc` |
| T13 | Old suites: vs1art 14/14, dialog 35/35 (routes updated for the GDD stages), battle3d 21/21, legacy 54/54 (goldens re-recorded only where boot `D` lines differ, checked as in `docs/SLICE_VS1_ART.md` §7) | the four suites |
| T14 | Assets reproducible: Corwin (body + head), portraits, grounds (`--check`), logo, props | `BPY=` / `VS1PY=` runs |
| T15 | KOS backend against stub headers in every flavour (+ `NO_FOG`, `GROUND_NO_VQ`) | `tests/dialog/run.sh` pattern |

---

## 12. Definition of done

### Functional
| ID | Criterion | Verified by |
|---|---|---|
| F1 | Title at dusk: logo fades in (45 t after T1), PRESS START blinks (30 t), menu NEW GAME / CREDITS, cleared crest after a clear; 40 s idle -> attract (if F4 is done) | T1, QC |
| F2 | NEW GAME: T2 with the text card (A skips the card), the intro cutscene ends with "Speak with Wren" and input on | T1, T6 |
| F3 | The village half plays exactly as slice 3D-5 (its routes and checks), with the GDD's stage numbers and lines restored (AD3/4/6/7 undone) | dialog suite |
| F4 | Corwin at (37,13): blocks the road before the token (walked back one tile), opens it with the token (S4, exit glow), hints at Soul Rend after; portraits x 3 | T2, T5 |
| F5 | The east exit (T7 20 + 20, 30 t walk-in) to the Barrow and back (BA3); the hero arrives facing the right way; no soft lock at either edge | T1, T2, T5 |
| F6 | The Barrow at night (preset, fog or its fallback), its props, its painted ground; the waystone (heal, C3, S5); the ring starts T8 (crane, name card, 3 lines; 60 t after the first time) and the boss battle | T1, T4, QC |
| F7 | Every lost battle -> T9 death screen per GDD 13 (timings, tint, tips, input after 60 t); RETRY restores the checkpoint (boss: short intro); RETURN TO TITLE via T11 | T4, T9 |
| F8 | The ending E1-E7 per GDD 14, one script, skippable where the GDD says; credits data-driven; the results card's numbers correct; back to the title with the crest | T1, T6, T10 |
| F9 | Every mode change is a catalogued transition with its exact ticks; a press-edge is needed after each; the closing A never starts a new interaction | T1, T9 |
| F10 | A new player finishes title -> results in 15-25 minutes without help (a play test) | you |

### Technical
| ID | Criterion | Verified by |
|---|---|---|
| TD1 | §9 budgets hold on every new scene (path sweeps) and, after Stage 0, **on hardware** (`DIAG` worst CPU frame < 16.7 ms at the title orbit, T8, the gathering, the boss) | T3, G4 |
| TD2 | Deterministic: float == double for every route; watched == skipped; the attract demo replays identically | T1, T6, T11 |
| TD3 | No heap after boot; every new pool fixed, overflow = a load-time error | T12, lint fixtures |
| TD4 | 0 warnings; ASan/UBSan clean over the full playthrough; the KOS backend compiles in every flavour; builds under DreamSDK | suites, you |
| TD5 | 0 TA stream errors, no dropped batch anywhere in the full playthrough | `drops` counters, DIAG |
| TD6 | Assets and grounds rebuild byte for byte | T14 |

### Process
| ID | Criterion |
|---|---|
| P1 | `docs/SLICE_BARROW.md` (built, measured, decided, deviations); HOWTO, TRIAGE, the hardware checklist updated; asset licences (Ranger: KayKit CC0) |
| P2 | Every deviation from GDD Appendix A marked `# slice 3D-6` in the scripts and listed in the report |
| P3 | Stage 0's hardware results recorded (they decide the budgets of this slice) |

---

## 13. Decisions and risks

### Decisions (recommendations; confirm or change)
| # | Question | Recommendation |
|---|---|---|
| D1 | Scope split: everything in §1.1 in one slice, or the title/attract/credits in a slice of their own | **One slice.** The GDD's definition of done is title -> results -> title; the title reuses the light presets and cutscene cameras this slice builds anyway. If time runs short, F4 (attract) and F5 (T5 iris) drop first |
| D2 | Scripts across maps | one compiled pool, `@map` per script, actor slots bound at map load (§5.4) |
| D3 | Waystone | a static-look NPC with `talk barrow_stone` (BA1) |
| D4 | Barrow exit back to the village | yes, closed once the Warden is down (BA3) |
| D5 | Second painted ground | reuse the VQ slot (`texture_update_vq`); grounds are 512x512 everywhere |
| D6 | Fog | PVR table fog with the `NO_FOG=1` kill switch; the night preset must read without it |
| D7 | Skip | fast-forward with presentation off (deterministic), never through a choice |
| D8 | Results rank | Gold < 20 min and 0 retries; Silver < 30 min and <= 2 retries; Bronze otherwise (data, tunable) |
| D9 | Logo | typeset from the dialog font by `make_logo.py` until A3 exists |
| D10 | Attract | recorded inputs of route 1's first minutes (BA5); should, not must |
| D11 | Audio | not in this slice; S12 hooks only; the audio track (AICA driver, streaming) plans separately |
| D12 | The ending's gathering spot | by the wagon (10,6), confirmed by the path sweep (BA2) |
| D13 | Budgets | keep 18,000 vertices / 1.60 M until Stage 0 measures hardware; then restate them in time (ms) |

### Risks
| Risk | Mitigation |
|---|---|
| **Slice 3D-5, the VS1 art and the VQ ground are unverified on hardware**; this slice would pile more on top | Stage 0 first; each has a kill switch (`PORTRAIT_KOS_TWIDDLE`, `GROUND_NO_VQ`, bodies back per model) |
| Frame budget in the new scenes (four VS1-headed characters in the ending; the title's orbit sees more of the village than the field camera) | path sweeps (T3) before content is final; gathering by the wagon; the orbit radius/height chosen by the sweep |
| Cross-map state bugs (defeated foes, checkpoints on another map, the VM across `map`) | Progress carries the map; byte-for-byte checkpoint tests at every checkpoint; the full playthrough with forced losses |
| Fog on hardware differs from the host emulation | `NO_FOG=1`; a night preset that works without fog; fog QC on Flycast and hardware early (E3 before E4) |
| Light lerps cost spikes (re-bake of every character, re-tint of every prop) | stepped lerps, work spread over frames like `bake_slice`; T8 counters |
| Romdisk/RAM growth (2.86 MB projected) | the total-RAM report (0.4); portraits could be compressed on the romdisk (LZ, decoded into the cache at boot) if 3.0 MB is not acceptable |
| Script scope creep (the GDD's command list is long) | the fixed command set of §5.5; anything else is code review |
| Texture slots (28 of 32 projected) | slot reuse for grounds; the logo and cards share the dialog atlas where possible |

---

## 14. Pointers for the implementers

### Where things are
| Concern | Files |
|---|---|
| Modes, fades, the party, battle hand-off, retry | `src/game.cpp/.h` (to be split into `game_flow`) |
| Field logic (hero, entities, props, talk, `if`) | `src/field.cpp/.h` |
| Field view (camera, ground, props, fades, two-shot, billboards, looks) | `src/field3d.cpp/.h` (tunables in `TUNES[]`, `--tune name=value` on the host) |
| Script compiler + VM | `src/script.cpp/.h`; its Python twin `tools/dialog/lint.py` (**parity is tested: change both**) |
| Dialog box, portraits, HUD text | `src/dialog_ui.*`, `src/portrait.*`, `src/text.*` |
| Quest state, checkpoints | `src/progress.*` |
| Battle (rules, AI, 3D staging, arenas) | `src/battle*.cpp`, `src/ai.cpp`, `src/skills.cpp`, `src/arena.cpp`, `romdisk/arenas/*.txt` |
| Renderer | `src/render3d.*` (static/skinned paths, near-plane strips, stats), `src/models.*` (registry) |
| Engine backends | `platform/kos/main.cpp` (PVR, textures, materials, boot checks), `platform/host/host_engine.cpp` (traces, dumps) |
| Host harness | `platform/host/trace_main.cpp` (options listed at its top), `story.cpp` (routes), `battle_bot.cpp` |
| Art pipelines | `tools/vs1art/` (VS1 props, heads, sweep map), `tools/ground/` (painted ground), `tools/battle3d/` (Blender converter, anims, props, FX), `tools/dialog/` (font, portraits, lint) |

### Conventions that the tests enforce
* **Determinism:** ticks and integers in logic; float == double traces for every route; exact atan2/sqrt only where a
  tie-break matters (see the comments in `field3d_tick`). New presentation clocks must not feed back into rules.
* **No heap after boot:** the host's allocation counter (`battle_trace_alloc`) must read 0 over every route.
* **Budgets are checked by the suites** (`--resources`, `V`/`B`/`S` lines). A new scene is not done until a sweep
  has measured it.
* **Assets are reproducible:** every generated file has a `--check` or a byte-identical rebuild in a suite.
* **Scripts:** every GDD deviation carries a `# slice 3D-6` comment with its reason.

### Lessons from slices 3D-5 and the VS1/ground passes
* **Sweep everything that moves the camera.** The hero-on-every-tile sweep found a 6,900-vertex near-plane case and a
  prop placement that cost 1.8k vertices; routes alone would have missed both. Build `--sweep-path` early (T3).
* The model arena and textures are **append-only** (`dmf::load`, `eng::texture_create`; no free): plan what both maps
  share; reuse texture slots with `texture_update`/`texture_update_vq` (the caller guarantees the slot was not drawn
  in the last 2 frames - a fade covers it).
* `eng::file_read` needs a buffer larger than the file (it NUL-terminates).
* The characters are **VS1 heads on our bodies**: new characters follow `tools/vs1art/build_chars.sh`; the eye glow
  comes from an untextured batch whose vertices must stay contiguous (`models_glow_range`).
* The painted ground is **stale** after any map edit: `paint_ground.py --check` fails in the suite; re-run it.
* PRELIT props ignore lighting: presets must tint their colour pools, not relight them.
* Field characters bake lighting per yaw at init (`bake_look`); anything that changes the light must re-bake (§5.6).
* The talk prompt, the two-shot (T-CAM) and the dialog UI have probes/tests (`--probe-talk`, `--probe-cam`,
  `check_dialog.py`): add Corwin and the waystone to them.
* `tests/vs1art/check_layout.py` asserts the VS1 props and the GDD entity positions: extend it (Corwin, exits), do not
  loosen it.
* Hardware is the reference; Flycast looks worse. Every KOS-only change needs a line in the hardware checklist and a
  kill switch if it is risky (fog, VQ, twiddling).
