# Next vertical slice: walk the field, touch an enemy, fight, return

Slice goal: **the hero walks across a tile-based stage. Touching an enemy or NPC starts combat. Winning returns him to the stage, where that foe is gone; losing restarts the stage.**

This document has four parts:
* §0: the cleanup already done, with how it was verified
* §1: findings that constrain the plan
* §2: stages and tasks
* §3: the definition of done (DoD)

---

## 0. Cleanup: done in this change

| Item | Change | Verification |
|---|---|---|
| Sprite resolution | `es_sprite_64.png` 1024×1024 → **64×64**. Lossless: the source was an exact 16× nearest-neighbour upscale (0 differing pixels on round-trip). **VRAM 2 MiB → 8 KiB.** 32×32 was rejected because it changes 185,856 source pixels | checked pixel by pixel in `tools/make_assets.py` |
| Unused asset | `plasma.png` deleted; nothing references it | `grep` finds only historical comments |
| Orb indicators → icons | new 128×128 atlas `romdisk/ui_icons.png` (1 texture slot, 32 KiB VRAM) with 13 placeholder icons: command ring sword / flame / shield, target arrow, enemy-turn chevron, hit spark, hurt burst, and A / d-pad / Start glyphs for the debug strip. Art is defined as text pixel grids in `tools/make_assets.py`, so it is reproducible and easy to review; preview in `docs/ui_icons_preview.png` | rendered previews; `docs/before_after.png` |
| Engine | `eng::sprite_draw_uv()` samples a sub-rectangle of a texture (atlas frames). KOS backend: 4 UV floats per quad on the same PVR path | stub type-check of the KOS backend, all DIAG variants |
| HP bars | stretched orb → framed, colour-coded bar (green/yellow/red) **below** each actor; status pips under the bar; distinct z per layer, so draw order doesn't depend on sort order | renders |
| Indicator rules | ring and previews only while choosing a command. The Lua build drew them during the animation too, covering the hit spark. The stray load-time stamp is removed | renders |
| **Game logic unchanged** | – | **138/138** suite runs identical to the original Lua in logic state (`compare.py --state-only`): every frame's state, active actor, turn order, HP and statuses |

Placeholder, not production: icons are 16×16 art drawn at 2× or 3×. They are consistent enough to keep as a style guide, but an artist should replace them.

Known visual limits, fixed by tasks below: opaque black boxes where icons overlap sprites (A2), and status words such as "BURN" drawn as dots (A6).

---

## 1. Findings that constrain the plan

1. **Input polarity is inverted.** Confirmed in your KOS's `cont_reply`: buttons are active-high, but `main.cpp` tests active-low. At rest, every button reads as held, which is why the overlay in your screenshot is all green. The battle UI works only because of `ui.lua`'s idle-baseline workaround. **Field movement uses *held* input, so with the bug as it stands the hero would walk in all directions at once. A1 must be done first.**
2. **No alpha.** Textures load as opaque RGB565. Characters over tiles need transparency (A2).
3. **Battle is a one-shot singleton.** `battle_init()` hard-codes the test encounter, and `win`/`lose` are dead ends. The slice needs a battle that can be entered repeatedly and returns a result (A3, A4).
4. **Timing is variable-`dt` float.** Grid movement and scripted tests need deterministic fixed ticks (A5). See REPORT §6.2.
5. **16 texture slots, no text.** The slice adds a tileset, a hero sheet, enemy and NPC sheets, and a font, so slots should be raised to 32. Text needs a bitmap font (A6).
6. The screenshot's scene is the **win** state: no enemies drawn, hero at ~2 HP, command ring still up. That is finding 3 in practice.

---

## 2. Stages

Sizes: **S** ≤ 1 day, **M** 2–3 days, **L** ~1 week, for one developer familiar with the code.

### Stage A: engine foundations (prerequisites; battle must still work after each task)
| # | Task | Size | Notes |
|---|---|---|---|
| A1 | **Input:** fix polarity (`(buttons & mask) != 0`; start `g_in.buttons` at 0); add `eng::Input` with `held / pressed / released` edges per frame plus d-pad auto-repeat; **remove `ui.cpp`'s idle baseline in the same change** | S | re-record battle golden traces; the debug overlay must read dark at rest |
| A2 | **Alpha sprites:** load with `PNG_MASK_ALPHA` (ARGB1555), draw in the **punch-through** list (needs `pvr_init(&params)` with a PT bin; `pvr_init_defaults` has none). Opaque tiles stay in OP. Per-sprite `list` flag | M | highest hardware risk: verify in Flycast **and** on hardware |
| A3 | **Game-mode manager:** `Boot → Field ⇄ Transition ⇄ Battle → GameOver`, each mode with `enter/update/draw/exit` | S | |
| A4 | **Re-entrant battle:** `battle_begin(const EncounterDef&, PartyState&)` / `battle_result()`; win and lose screens (banner, press A to continue); hero HP written back to `PartyState` | M | the encounter table replaces the hard-coded `spawn_test_slimes` |
| A5 | **Fixed 60 Hz timestep:** integer tick accumulator in `main.cpp`; gameplay timers in ticks | S | tests become frame-exact on float hardware |
| A6 | **8×8 bitmap font** (generated by `tools/make_assets.py`) for status words, WIN/LOSE and debug text | S | replaces the 7-segment fallback dots |
| A7 | Raise sprite slots 16 → 32; log VRAM use per texture in DIAG builds | S | |

### Stage B: field
| # | Task | Size | Notes |
|---|---|---|---|
| B1 | **Tile map:** ASCII map files on the romdisk (`maps/test_field.txt`: tile legend, collision, entity lines), parsed at boot. Tileset atlas with 16×16 art drawn at 2× (32 px tiles, 20×15 per screen). Draw only visible tiles; camera follows the hero and is clamped to the map edges | M | ASCII keeps the slice simple; Tiled (.tmx → converter) can come later |
| B2 | **Hero:** 4-direction sheet, ≥ 2 walk frames per direction; **grid-step movement** (one tile per step, interpolated over a fixed number of ticks); facing; holding a direction keeps walking; walls and entities block | M | grid movement gives a clear definition of "touch" and deterministic tests |
| B3 | **Entities:** from map data. `type {Enemy, Npc}`, tile, facing, sprite, `on_touch = StartEncounter(id)`, idle animation; sorted by y for drawing | S | |
| B4 | **Touch → combat:** stepping *into* an entity's tile, or being stepped into by a patrolling one, triggers its `on_touch`. Field input is frozen from then on | S | NPC dialogue is out of scope, see open questions |
| B5 | **Transitions:** field → battle fade (≤ 0.5 s) with the touched entity's encounter; win → fade back to the same tile, foe removed for the session, HP kept; lose → Game Over → A restarts the map (full HP, foes restored) | M | |
| B6 | **Placeholder art** via `make_assets.py`: tileset (floor, wall, water/decoration), hero walk sheet, slime and NPC sheets. Replaces the ship as the enemy sprite in battle | M | |

### Stage C: verification and hardening
| # | Task | Size |
|---|---|---|
| C1 | Extend the host harness to the field: trace lines for hero tile and facing, camera, mode and entity flags; scripted field inputs (walk, collide, trigger, win-return, lose-restart); **golden traces** stored in `tests/golden/` | M |
| C2 | Frame-time probe (`timer_us_gettime64` around update/draw) in DIAG builds; 5-minute soak in Flycast, plus hardware if available | S |
| C3 | `render_trace.py` previews of field frames, for review without hardware | S |

**Order:** A1 → A5 → A3 → A4 → A2 → B1 → B2 → B3 → B4 → B5. A6, A7, B6 and C1 run alongside. A1 comes first because B2 cannot work without it.

---

## 3. Definition of done (slice)

The slice is done when **every** item below is true on the main branch. "Test" means an automated host-harness script with a golden trace. "Flycast" means a manual check on the current DreamSDK build, and on hardware if available.

### Functional acceptance
| ID | Criterion | Verified by |
|---|---|---|
| F1 | Boot goes straight to the field map `test_field`, which is larger than one screen (≥ 30×20 tiles). It uses ≥ 3 tile types, including a blocking wall type, with the hero at the spawn tile | test + Flycast |
| F2 | D-pad moves the hero one tile per step in 4 directions, with a walk animation and correct facing. Holding keeps walking; releasing stops on a tile boundary | test + Flycast |
| F3 | Walls and map edges block movement. The hero never overlaps a blocking tile or leaves the map | test (collision sweep script) |
| F4 | The camera follows the hero and clamps at the map edges (no void visible) | test (camera coordinates) + Flycast |
| F5 | The map has ≥ 2 enemies and ≥ 1 NPC, all placed **from the map file** (no code change to move them), with idle animation | review + test |
| F6 | Touching an enemy **or an NPC** starts a fade and then combat with **that entity's** encounter; field input is ignored during the transition | test |
| F7 | Battle shows the new icons, **no orb placeholder**, working B-cancel and Y-shortcut, and readable status words | Flycast + render preview |
| F8 | **Win:** a result banner, then A returns to the field at the tile where contact happened. The defeated entity is gone; the hero's HP is carried over | test |
| F9 | **Lose:** a Game Over screen, then A restarts `test_field` with full HP and all entities restored | test |
| F10 | Input is correct at rest: the debug overlay and debug strip show no buttons held when the pad is untouched | Flycast |

### Technical acceptance
| ID | Criterion |
|---|---|
| T1 | Builds clean with DreamSDK (`make`) and on the host (`make host`), `-Wall -Wextra`, 0 warnings; `make DIAG=1` and all `DIAG_SKIP` variants build |
| T2 | **Steady 60 fps:** DIAG frame probe shows 0 frames over 16.7 ms during a 5-minute Flycast session covering walking plus 3 battles (repeat on hardware if available) |
| T3 | No heap allocation inside the per-frame loop (fixed-capacity containers; enforced by review and a DIAG allocation counter) |
| T4 | Texture VRAM ≤ 1 MiB total, logged per texture at boot in DIAG; sprite slots used ≤ 32 |
| T5 | Characters and icons render with punch-through alpha: **no black boxes** over tiles or other sprites |
| T6 | Battle rules unchanged: the battle golden traces (re-recorded after A1) pass with `--state-only`, **plus** deliberate rule fixes only as separate, reviewed changes |
| T7 | Deterministic: the same input script gives the identical trace on repeated runs and with `real=float` vs `double` (fixed timestep) |
| T8 | All slice test scripts pass in `harness/run_suite.sh`; runtime ≤ 5 minutes |
| T9 | No `LEGACY:` behaviour added; existing `LEGACY:` markers touched by this slice are resolved or ticketed |

### Documentation and process
| ID | Criterion |
|---|---|
| P1 | `docs/` explains how to add a map, an entity, an encounter, a skill and an icon (one short page each) |
| P2 | The placeholder-art pipeline (`make_assets.py`) produces every slice asset; the committed PNGs match the script output |
| P3 | `REPORT.md` §5 bug list is updated: items fixed in this slice are marked, with the commit |
| P4 | Demo video or GIF captured from Flycast showing F1–F9 in one take |

---

## 4. Risks and open questions

| Risk / question | Mitigation / proposal |
|---|---|
| Punch-through list setup (A2) is the only new PVR feature | build it first as a standalone DIAG scene; check in Flycast **and** on hardware before Stage B |
| **Should NPC contact start combat?** (Taken literally from the brief: "touches an enemy/npc → combat") | DoD F6 follows the brief. Non-combat NPC dialogue needs a text box, which would be the next slice; `on_touch` already allows for it |
| Grid-step vs free movement (Mario RPG uses free isometric movement) | grid for this slice: simpler collision, exact "touch" semantics, deterministic tests. Free movement would be a later slice |
| Tile and sprite scale | proposal: all field art is 16 px drawn at 2× (matches the icons); battle sprites 32 px at 2× |
| The battle layout assumes 1 hero vs 3 enemies at fixed positions | the encounter definition carries formation positions (A4) |
| Art direction: the ship sprite is used for the hero and all enemies | B6 placeholders; final art is not part of this slice |

---

## 5. Implementation status (slice delivered, awaiting Flycast / hardware sign-off)

Everything in §2 is implemented. Every item below that can be checked without a Dreamcast has been checked on the host.

* **"Verified"** means automated evidence exists: `tests/run_tests.sh` has 54 checks and runs in about 20 s.
* **"Flycast"** means someone must look at it on the real build.

### What was built
| Task | Implementation | Evidence |
|---|---|---|
| A1 input | KOS polarity fixed (`!= 0`, start state 0). `src/input.*` provides held/pressed/released per tick. The UI idle baseline is removed in the same change | Lua reference patched with the **same** fix: **138/138** logic-identical |
| A5 fixed tick | Exact 60 Hz accumulator in `main.cpp` (µs × 60 vs 1,000,000; at most 4 catch-up ticks). Battle timers are integer ticks, calibrated from float32 at 60 Hz | tick build == old float32 build at dt = 1/60: **138/138**; tick float == tick double: **138/138** |
| A3 modes | `src/game.*`: Field → ToBattle (0.4 s fade) → Battle → ToField → Field, or → GameOver → retry; plus a battle-test entry for the harness | `field_win`, `field_lose`, `field_full` scenarios |
| A4 re-entrant battle | `battle_begin(EncounterDef, PartyState)`; win/lose banner, A to continue; hero HP written back. Encounters are a data table (`src/encounters.cpp`) | battle-test build == A5 build: **138/138** |
| A2 alpha | ARGB1555 (`png_to_img` mask 1) in the **TR list**; z from draw-call order; vertex tint/alpha | KOS behaviour read from your ELF: mask 1 → format 5 (ARGB1555); TR defaults honour texture alpha (bit 19 clear) and modulate alpha; autosort on |
| A6 font | 5×7 font (47 glyphs), `font_draw` with drop shadow; status words and banners use it | renders |
| A7 slots | 16 → 32 texture slots; DIAG logs VRAM per texture and the total | 6 slots / 192 KiB used |
| B1 map | ASCII map loader with line-numbered errors; 6 tile types (water animated); visible-window culling; clamped camera | `field_walk` collision sweep; invariants |
| B2 hero | ~~grid steps~~ **free 8-direction movement** (changed after play-test, see §6); 4 facing sprites; walk cycle | traces + renders |
| B3/B4 entities | enemy/NPC from map lines; slime squash / guard blink idle; **patrol** with turn-around; touch in both directions | 40 fuzz sessions: 24 battles from all 3 entities, 4 started by the patrolling enemy walking into the hero |
| B5 transitions | fades; win → same tile, foe removed, HP kept; lose → GAME OVER → A → full reset | invariants F8/F9 with expected counts |
| B6 art | hero, guard (palette swap), slime, 7 tiles, font: `tools/make_assets.py` | `docs/slice_art_preview.png` |
| C1 tests | `tests/`: 22 committed input scripts, gzipped golden state traces (324 KB), invariant + UI checkers, runner | 54/54 pass |
| C2 perf probe | DIAG: worst CPU frame, frames over 16.7 ms, heap in use with delta, every 300 frames | needs a DIAG run |
| C3 previews | `tools/render_trace.py` updated for alpha, tint and the z model | `docs/slice_playthrough.png` |

### Deviations from the plan (deliberate)
* **A2 uses the TR list, not punch-through.** `pvr_init_defaults` in your KOS has the PT bin size set to 0, confirmed by decoding it. TR needs no `pvr_init` change, which removes the riskiest item. The cost is blending fill-rate, which is negligible at about 50 alpha sprites.
* **A1 has no d-pad auto-repeat.** Nothing in this slice needs it: menus use taps, and the field reads held buttons.
* **B4 patrol is implemented** (it was optional). **Battle art uses the new sheets**; the ship sprite is unused and deleted. Characters draw with nearest filtering (pixel art) instead of the old bilinear on enemies.
* Test scripts drive battles with **A and Y only**, so leftover menu presses can't move the hero after a battle. The checker requires **expected win/restart counts**, because a mutation showed it could otherwise pass vacuously when no battle ever starts.

### Definition of done: status
| ID | Status | Evidence / what remains |
|---|---|---|
| F1 | Verified (host) · Flycast | 30×20 map, 6 tile types incl. blocking ones; boot trace tick 1 = `mode=field` at spawn |
| F2 | Verified (host) · Flycast for feel | free 8-direction movement at 2 px/tick (diagonals normalised), wall-sliding, stable facing, walk cycle (`field_walk`, `field_diag`, renders) |
| F3 | **Verified** | invariants on 9 scenarios + 40 fuzz sessions (240k ticks); mutation "water walkable" caught |
| F4 | **Verified** | camera clamp + hero-on-screen invariant on every tick |
| F5 | **Verified** | 2 enemies (1 patrolling) + 1 NPC, all from `maps/test_field.txt`; idle animations |
| F6 | **Verified** | 24-tick fade; encounter taken from the touched entity; field frozen during fades; both touch directions |
| F7 | Verified (host) · Flycast | no orb asset exists; A/B-cancel/Y-shortcut asserted by `tests/check_ui_buttons.py`; "GUARD"/"BURN" in the font |
| F8 | **Verified** | invariant: same tile, foe gone, HP kept; expected win counts; 2 mutations caught |
| F9 | **Verified** | invariant: spawn, full HP, all entities restored; 2 mutations caught (incl. "defeated foe stays gone") |
| F10 | Flycast | polarity proven from the controller driver's disassembly; needs a look at the overlay at rest |
| T1 | Verified (host + KOS type-check) · DreamSDK build | 0 warnings: host `-Wall -Wextra -Wshadow -pedantic`; KOS backend in all 5 DIAG variants. **Note:** the first DreamSDK build failed (`int` → `pvr_list_t`), because the earlier API stub used `int` where KOS 2.1 has typed enums. The stub is now generated from the DWARF signatures of your own build, and it reproduces that exact error on the old line |
| T2 | **Flycast/hardware** | run `make DIAG=1` and read "frames over 16.7 ms". Host: 2.3 µs/tick field, 0.25 µs/tick battle |
| T3 | Verified by design · hardware | `FixedVec` everywhere in game code; KOS queues keep their capacity; DIAG prints the heap delta (expect 0 after warm-up) |
| T4 | **Verified** | 6 textures, 192 KiB, 6/32 slots |
| T5 | **Flycast** | alpha path checked against the KOS binary; needs visual confirmation of no black boxes |
| T6 | **Verified** | 12 battle goldens traced back through A4 → A5 → A1 → Lua (each step 138/138) |
| T7 | **Verified** | float == double: identical logic traces on all 21 tests, 40 fuzz sessions and 138 battle inputs |
| T8 | **Verified** | 54 checks, about 20 s |
| T9 | **Verified** | no new `LEGACY`; the 7 remaining are untouched gameplay items, ticketed in REPORT §5 |
| P1 | **Done** | `docs/HOWTO.md` (map, entity, encounter, skill, status, art, tests) |
| P2 | **Verified** | the runner regenerates the art and compares checksums |
| P3 | **Done** | REPORT §5 updated |
| P4 | **You** | Flycast capture of F1–F9 |

**Sign-off procedure:**
1. `make clean && make` under DreamSDK; play through `field_full`'s route (win against the top slime, lose to the guard, retry).
2. `make clean && make DIAG=1`; play for 5 minutes and read the DIAG report lines (T2/T3/T4).
3. If anything crashes, follow `TRIAGE.md`; `DIAG_SKIP` now bisects `game` / `update` / `draw`.

---

## 6. Play-test cleanup (after the first Flycast run)

| Request | Change | Evidence |
|---|---|---|
| "Only A is available; there should be a B that goes back a step" | B is **back one step** in every selection state: Attack targeting → ring (Attack still highlighted), Fire confirm → ring (Fire still highlighted). A **prompt bar** shows the buttons that work in the current state (A SELECT / ◀▶ CHOOSE / Y GUARD, A ATTACK / ◀▶ TARGET / B BACK, A CAST FIRE / B BACK); the selected command's name is shown above the hero. The debug strip now shows A, B, Y, d-pad and Start | `tests/check_ui_buttons.py buttons` |
| "Fire casts immediately; it should mark every enemy and need a second A" | New UI state **TargetAll**. A on Fire shows a bobbing marker over **every** enemy plus "ALL ENEMIES"; A (after the same 0.27 s entry lock as Attack) casts, B goes back | `tests/check_ui_buttons.py fire` checks that nothing is cast before the confirming A and that it casts on exactly that tick. Mutations caught: immediate cast, B ignored, B losing the Fire highlight. **Rules unchanged:** the Fire scenario gives the identical sequence of 11 turn/HP/status states before and after |
| "Locked to 4 directions is uncomfortable; allow diagonals" | **Free 8-direction movement**: positions are integers in 1/16 px (deterministic); 2 px/tick straight, 23/16 px per axis on diagonals (same speed); 16×12 feet hitbox moved one axis at a time, so the hero **slides along walls**; facing stays stable on diagonals; touch = hitbox contact, including a patrolling enemy walking into a standing hero | `field_diag` (681 diagonal ticks, wall-sliding checked in the trace), `field_patrol` (the enemy walks into the standing hero at tick 437), 40 fuzz sessions with diagonal input (39k diagonal ticks, 31 battles): all invariants OK, float == double. **8/8** movement mutations caught |

Battle goldens that did **not** change (byte-identical logic): Attack win, Guard lose, hold-A, lock-edge; the Attack and Guard flows are untouched. Re-recorded on purpose: the Fire-using battles and all field scenarios (new movement model and trace format). Test suite: **61 checks**.

