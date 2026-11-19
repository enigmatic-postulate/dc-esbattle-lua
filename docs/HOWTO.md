# How to extend the game (DoD P1)

Every change below ends the same way: run `tests/run_tests.sh`. If the change *should* alter behaviour, check the failure diff, then re-record with `tests/run_tests.sh --update`. To look at a frame without hardware, use `tools/render_trace.py` (see "Preview without hardware" at the end).

---

## 1. Add or edit a map

Maps are plain text on the romdisk: `romdisk/maps/<name>.txt`. The boot map is set in `platform/kos/main.cpp` (`bt::game_init("/rd/maps/test_field.txt")`) and the host driver (`--map`).

```
# comment lines start with '#'
name test_field
map 30 20                 <- width height (max 64 x 64), followed by exactly <height> rows
##############################
#....,.....=......~~~~~......#
...
spawn 3 10 right          <- hero start tile + facing (down|up|left|right)
entity slime_a enemy slime 12 4 down slimes_2
entity slime_b enemy slime 20 12 left test_slimes patrol 1 0 3
entity guard enemy guard 10 16 down guard_duel
entity wren npc mage 6 17 up talk wren_main      <- slice 3D-5: NPCs talk (a script), enemies fight
```

| Char | Tile | Walkable |
|---|---|---|
| `.` | grass | yes |
| `,` | flowers | yes |
| `=` | path | yes |
| `#` | wall | no |
| `T` | tree | no |
| `~` | water (animated) | no |

* The loader rejects bad files with a line number: unknown tile char, wrong row width, spawn or entity on a blocking tile, unknown encounter, an `npc` without `talk`. The message goes to the serial log, and the game shows **MAP LOAD ERROR**.
* The field is drawn in 3D (`src/field3d.cpp`, see §8): the ground plane is one texture over the whole map - painted offline for the village (`romdisk/maps/village.gnd`, `tools/ground/paint_ground.py`, docs/GROUND_ART.md: re-run it after editing the map), else generated from the tiles when the map loads (VS1's ground, 4-8 texels per tile). The logic still works in 32 px tiles (1/16 px positions). Maps can also place 3D props (§8). Outside the map the hero is blocked, so a map needs no `#` border (the village has none).
* Movement is free and analog (stick, or the d-pad as a full-tilt stick): up to 24 sub-px/tick (1.5 px/tick = 2.81 tiles/s), the same in every direction. The hero collides with a 16×12 "feet" box (`HERO_BOX_*` in `src/field.h`), so 1-tile corridors are easy to walk, and he slides along walls. Entities block with a 24×20 body box; touching one starts its encounter.
* After editing the test map, re-check the collision sweep `field_walk` (its route is written for this map).

## 2. Add an enemy or NPC to a map

```
entity <id> npc   <look> <x> <y> <facing> talk <script> [patrol <dx> <dy> <len>] [if <cond...>]
entity <id> enemy <look> <x> <y> <facing> <encounter> [patrol <dx> <dy> <len>] [after <id> [<id>]] [win <flag>] [if <cond...>]
```

* `id`: unique per map, at most 15 characters. It appears in test traces and is the actor name scripts use.
* `look` (slice 3D-5): `slime` (Skeleton Warrior), `guard` (Skeleton Mage), `mage` (Wren), `smith` (Brom). New looks:
  `LOOKS[]` in `src/field3d.cpp` + `EntityLook` in `src/field.h` + `look_by_name()`.
* **enemy**: `encounter` must exist in `src/encounters.cpp` (see §3). Touching it starts that battle. `win <flag>`
  sets a quest flag when its battle is won; `after a [b]` keeps it dormant until those are beaten.
* **npc** (slice 3D-5): `talk <script>` names the script A runs (§12). Touching an NPC never starts a battle: it
  blocks like a wall. It stops its patrol and faces the hero while he is within 3 tiles.
* `if <cond...>`: the rest of the line is a condition (§12 syntax); false = hidden, untouchable, frozen. Re-evaluated
  whenever the quest state changes (the Warden: `if has_token`).
* `patrol 1 0 3`: walks back and forth 3 tiles along +x, waiting 45 ticks between steps. Exactly one of dx/dy is
  non-zero (+-1). An enemy that walks into the hero starts its battle; an NPC waits.
* Limit: 16 entities per map (`MAX_ENTITIES` in `src/field.h`). A won battle removes the entity; a lost one restores
  the last checkpoint (entities beaten before it stay beaten).

## 3. Add an encounter

Edit the table in `src/encounters.cpp`:

```cpp
{ "bats_3", 3, { { "bat_a", SHEET_SLIME, 8, BAT, 460, 200 },
                 { "bat_b", SHEET_SLIME, 8, BAT, 500, 260 },
                 { "bat_c", SHEET_SLIME, 8, BAT, 460, 320 } } },
```

* Fields per enemy: id, sprite sheet, HP, `Stats{str, def, mdef, int, agi}`, and screen position (the hero stands at 180,240).
* Up to 4 enemies per encounter.
* Turn order is by `agi` descending; ties keep the listed order.
* Damage is `max(1, str − def)` (physical) and `max(1, int − mdef)` (magic; Fireball deals 65 % of that). Check that the numbers make the fight winnable.
* `test_slimes` is the regression encounter: **do not change it**, or the battle goldens will change.

## 4. Add a skill

(For the 3D battle a skill also needs a sequence: §11.)

1. In `src/skills.cpp`, write `resolve` (rules: damage, statuses; return a `SkillResult`) and, optionally, `present` (animation and floating text only). Add a row to `SKILLS[]` and a value to `SkillIndex` in `skills.h`.
2. Target modes: `Self`, `EnemySingle` (the player picks a target), `EnemyAll` (AOE, targets chosen automatically), `AllyAll`, `None`.
3. To make it selectable, the command ring in `src/ui.cpp` currently has 3 fixed slots (`RING_ICON[]`, `home_i` 0..2). Adding a 4th slot means extending the wrap arithmetic and the `ui_update` branches.
4. Durations are in **ticks** (60/s). Reuse `T_MOVE`, `T_FLASH_*`, `T_HURT_*`.
5. Rule changes belong in `resolve`, never in `present`. (Fireball's burn is the one `LEGACY` exception.)

## 5. Add a status effect

`src/status.cpp`: add a `StatusId` value (in `battle_state.h`) and a row in `DEFS[]` with optional hooks `on_apply / on_turn_start / on_turn_end / on_expire` and `skip_turn`. Give it a pip colour in `src/hpbar.cpp` (`status_pip_color`). Statuses tick down at the **end of the owner's turn**, which is why Guard currently expires too early (REPORT §5 #1).

## 6. Add an icon, tile, character or glyph

All placeholder art is defined as text pixel grids in `tools/make_assets.py`, 16×16 art stored at 2×:

* **Icon:** add `("name", GRID)` to `ICONS`, and the matching `ICON_*` value in `src/icons.h`, **in the same order**. Draw with `icon_draw(ICON_X, cx, cy, ICON_2X)`; use sizes 16/32/48 to keep pixels crisp.
* **Tile:** append to `TILES`, add a value to `enum Tile` (same order) in `src/field.h`, and give it a map character in `tile_from_char()` (plus `tile_walkable()` if it is walkable).
* **Character:** 4 rows (down, up, left, right) × 4 columns (stand, walk A, walk B, extra). Add it to `main()`'s `sheet(...)` calls, then to `CharSheet` / `chars_init()` / `sheet_by_name()` in `src/chars.*`.
* **Font glyph:** add a 5×7 entry to `FONT` (`'#'` = pixel).
* Regenerate with `python3 tools/make_assets.py`. The test suite fails (P2) if the committed PNGs don't match the script output.
* Budget: every texture here is 128×128 16-bit (32 KiB). Keep the total under 1 MiB and at most 32 textures; DIAG builds log the running total at boot.

## 7. Tests

| Command | What it checks |
|---|---|
| `tests/run_tests.sh` | builds the host binaries, then 12 battle regressions (against the golden state traces) + 2 battle-UI flow checks (B back, Fire confirm) + 11 field scenarios (goldens + invariants F3/F4/F8/F9 + expected win/restart counts) + float==double determinism + asset reproducibility. 61 checks, about 25 s |
| `tests/dialog/run.sh` | slice 3D-5: lint and its fixtures, compiler/metrics parity, the talk prompt, the dialog UI rules, the story routes (order variations), checkpoints, the Fire lock, budgets (resources, vertices, heap, SH4 estimate), sanitizers, assets. ~8 min; `QUICK=1`, `BPY=`, `SH4=1` |
| `tests/battle3d/run.sh` | slice 3D-4: the 3D battle, the rules equivalence, boss balance |
| `tests/run_tests.sh --update` | re-records `tests/golden/*.trace.gz` after an **intentional** change; review the diff first |
| `python3 tests/check_invariants.py <trace>` | field rules on any trace (e.g. long fuzz runs) |
| `python3 harness/gen_inputs.py field_fuzz ms60 <seed> 6000 x.in` | new random field session |

Input scripts are one line per 60 Hz tick: `<dt> <buttons-hex> [<stick-x> <stick-y>]` (dt is ignored; the stick columns are optional, -128..127, + = right/down). Bits: up 1, down 2, left 4, right 8, A 16, B 32, Y 64, Start 128; combine bits for diagonals (up+right = 09). Walking straight at full tilt covers one tile per ~21 ticks.

The 3D slice adds `tests/slice3d/run.sh` (see `docs/SLICE_3D_1.md` §6): runtime vs reference, validator cross-check, sanitizer fuzz, movement invariants, float == double, battle traces vs a baseline (`BASELINE=<dir>`), and SH4 runs under qemu (`SH4=1`).

**Building for the Dreamcast.** The makefile tracks header dependencies (`.d` files) since the VS2 fix-up. Run `make clean` once after updating from an older tree, and whenever you change a build flag (`DIAG=1`, `R3D_SAFE=1`, `PVR_DEFAULTS=1`, `SHZ_DEBUG=1`, `HOT_OPT=`). Every build prints `[boot]` breadcrumbs on the serial log; `TRIAGE.md` (last section) lists the expected sequence.

**SH4ZAM (required since the SH4ZAM slice).** The 3D math runs on the SH4ZAM library; see §10. Install its KallistiOS port once (DreamSDK Manager > KallistiOS Ports > SH4ZAM; or `cd $KOS_PORTS/sh4zam && make install`), or build against a checkout with `make SH4ZAM_DIR=<path>`. The host build (`make host`, `tests/run_tests.sh`) needs a checkout and CMake: it finds `SH4ZAM_DIR`, `external/sh4zam` or `../sh4zam/sh4zam`.

**Battle controls.** Command ring: ◀▶ choose (Attack, Fire, Guard, Potion), A select, Y jumps to Guard. Attack → pick a target (◀▶ in screen order, A), B back. Fire → confirm step with every enemy marked (A cast), B back. Guard casts immediately; Potion too (greyed with none left). During a sequence, hold A to play it at 2x; A skips the battle intro.

## 8. The 3D village (field)

The boot map is `romdisk/maps/village.txt`: since the VS1 art reconciliation (§13, `docs/SLICE_VS1_ART.md`) VS1's
40x30 layout, the one the GDD describes. `test_field.txt` still loads with `--map` on the host.

Besides the tile rows, `spawn` and `entity` lines, a map can place 3D props:

```
prop <model> <x> <y> <rot_deg> [solid <w> <h>]
prop house_stone   8.0  10.0    0 solid 4 4     # door faces south (rot 0); blocks a 4x4 footprint
prop house_stone   9.0  19.0  180 solid 4 4     # door faces north
prop crate         5.5  12.5    0 solid 1 1
prop bush          5.4   9.2    0               # decoration: does not block
```

**Placement:**
* `model` = `romdisk/models/<model>.dmf` (name up to 15 characters). If `<model>_r<yaw>.dmf` exists for the prop's yaw, it is used instead: the model with the faces the camera can never see at that yaw removed. The village props are all such VS1 variants (`house_plaster_r0/_r180`, `house_stone_r0/_r180`, `arch_r90`, `border_r0`, `boulder02_r0/_r90`, `boulder03_r0/_r90`, `crate_r0`, `fence_r0`, `wagon_r0`), with their lighting baked: placing one at a yaw VS1 did not build needs a new variant (§13).
* Numbers are `[-]digits[.digits]`, e.g. `12`, `-0.5`, `21.25`. No exponents. Footprint sizes are whole numbers.
* `(x, y)` is the model origin, in tiles. Fractional positions are fine.
* A solid footprint is centred on `(x, y)` and must be tile-aligned. The loader checks this, and checks that the footprint stays inside the map.
* Footprint tiles block exactly like walls. Spawn and entities may not stand on them.

**Tiles that get 3D models automatically:**
* `#` tiles: a fence segment is drawn between every pair of neighbouring `#` tiles, so runs and corners join up.
* `T` tiles: a tree, with yaw and size varied per tile (deterministic).

**Entities:**
* `slime` enemies draw as the skeleton warrior; the `guard` draws as the skeleton mage.
* Patrols move at 1 tile/s (`NPC_STEP_TICKS` = 60).

**What the view does:**
* Houses, boulders, the wagon, the arch and trees that cover the hero on screen fade to `fade_alpha` (fainter the closer they are to the eye: `near_fade_*`, VS1's rule): front faces only, TR list.
* Static props have their lighting baked once per instance; the VS1 props (`PRELIT`, docs/DMF.md) come baked.
* The camera is VS1's: 9 up, 9 back, 32-degree lens, aimed 1.5 tiles north of the hero, leading his motion, clamped to the map (`cam_*` tunes).
* Enemies and the hero use lighting baked per yaw (`hero_dyn_light 1` lights the hero per vertex instead).
* The village's frame cost depends on the geometry: see `docs/SLICE_3D_2.md` §4 before adding many props in one view.

**Adding a prop:** see §13 (a VS1 prop) - or any static `.dmf` in `romdisk/models/` (e.g. `tools/battle3d/make_props.py`
builds procedural ones), placed with a `prop` line. Check the result with a host frame dump (below) and the frame cost
over the whole map: `battle_trace_f32 - /dev/null --sweep > s.txt && python3 tools/vs1art/sweep_map.py s.txt romdisk/maps/village.txt map.png`
(the budget: <= 18,000 written vertices on every tile, `tests/vs1art/run.sh`).

## 9. 3D models and the 3D field

**Models** are `.dmf` files in `romdisk/models/` (format: `docs/DMF.md`). Rebuild all of them with `tools/dmf/build_models.sh`:
* The hero needs Blender's Python: `python3.11 -m venv .venv && .venv/bin/pip install bpy==4.2.0 numpy pillow`, then `BPY=.venv/bin/python tools/dmf/build_models.sh`.
* The output is byte-for-byte reproducible.

**Another KayKit character** (all share `Rig_Medium`, so the walk and idle come along):
```sh
.venv/bin/python tools/dmf/kaykit_to_dmf.py --character ART/Barbarian.fbx --texture ART/barbarian_texture.png \
    --anim Walking.fbx --occlude Head=BearHat --tris 1500 --arm-spread 20 --out romdisk/models/barbarian.dmf \
    --report assets_src/barbarian_report.json
python3 tools/dmf/preview_dmf.py romdisk/models/barbarian.dmf /tmp/b.png --anim walk --frames 8
```

Converter options:
* `--drop`: removes parts.
* `--occlude PART=COVER`: deletes faces of PART that COVER hides (do this for anything under a hat or helmet, or it pokes through after decimation).
* `--part-weight NAME=W`: shifts the triangle budget between parts.
* `--double-sided`: parts drawn without culling (capes).
* `--tex-size`.

Check the report for triangles per part and UVs near swatch edges.

**Drawing a model** (see `field3d.cpp`):
1. `dmf::load` once.
2. Materials: `eng::material_create(model.texture, eng::MAT_BILINEAR)`.
3. Per frame:
   * `dmf::pose_sample` (and `pose_blend`);
   * `dmf::pose_skin`;
   * `r3d::draw_model(cam, light, model, mats, world, skin)`.

Static models pass `nullptr` for the skin.

**Budget per model:**
* Keep heroes around 1500 triangles. The Knight costs ~320k SH4 instructions per frame (transform, lighting, strips).
* Keep one 64×64 16-bit texture per model (8 KiB VRAM).
* Up to 4096 vertices and 32 bones.

**Tuning the view** without rebuilding (host): `--tune name=value` on `battle_trace_f32`. Names are in `TUNES[]` in `src/field3d.cpp`: `cam_pitch`, `cam_dist`, `cam_fov`, `cam_look_h`, `hero_scale`, `walk_max_rate`, `shadow_alpha`, …. Render the result with `--dump` (below). Make the chosen values the defaults in `field3d.cpp`.

**Which entity gets which model:** slime sheets use the skeleton warrior, everything else the skeleton mage (`field3d_draw`).

## Preview without hardware

```sh
./build/battle_trace_f64 tests/inputs/field_full.in /tmp/full.trace        # full trace (with draw calls)
python3 tools/render_trace.py /tmp/full.trace 60 romdisk /tmp/frame60.png   # approximate PVR render of tick 60 (2D)
```

3D field frames (vertices are too bulky for traces, so selected ticks are dumped):

```sh
./build/battle_trace_f32 tests/inputs/field_full.in /tmp/t.trace --dump /tmp/frames --dump-frames 60,120
python3 tools/render_frame.py /tmp/frames/frame_00120.txt romdisk /tmp/frame120.png
```

`--view` adds a `V` line per field tick (camera, hero yaw, walk blend, triangles, vertices, arena use).

## 10. The SH4 math layer (SH4ZAM)

The renderer's math goes through SH4ZAM (`docs/SLICE_SH4ZAM.md`), linked as a library and never copied into this tree, as SH4ZAM's AGENTS.md asks. `src/hwmath.h` is the one include point and checks the version (0.10.0 or newer). On the Dreamcast this compiles to FTRV (matrix × vector in the XMTRX register bank), FSCA (sine and cosine), FSRRA (1/sqrt, used for sqrt, normalise and the perspective divide) and MOVCA.L/PREF (cache). The host harness builds SH4ZAM's portable C back-end from the same calls.

When you touch the 3D code:
* **Use `src/math3d.h`.** It keeps the renderer's types (`Vec3`, `Quat` = `shz::quat`, row-major `Mat34`) and forwards to SH4ZAM. For many points against one matrix: `xmtrx_load(m)` (and `xmtrx_apply(...)` for products), then `xmtrx_point(p)` per point. For a single point, use `mat_point` (no XMTRX).
* **XMTRX is scratch.** Load it, run the batch, and call nothing that reloads it in between (`mat_mul`, `mat_look_at`, `mat_from_qt`, `camera_look`, `emit_poly`, `box_vis`...). `vtx_begin`/`vtx_end` are safe.
* **No pointer casts between the game's types and SH4ZAM's.** Convert vectors component by component (`sv()`/`rv()`), and pass matrices through `Mat34::rows`. A cast is type punning, and GCC's strict-aliasing optimisation broke exactly that on the SH4 build (`docs/SLICE_SH4ZAM.md` §6.1).
* **`shz_invf_fsrra(W)` needs W > 0**: the vertex paths clamp W to `near_w` first. `shz_sincosf` takes angles in 1/65536 turns (about 0.0055° steps). `shz_fminf`/`shz_fmaxf`/`shz_clampf` are NaN-aware library calls on the SH4 without `-ffast-math`: keep plain compares in loops.
* **Vertex writes**: a whole 32-byte vertex written to the arena or to `g_xv` goes through `put_vert()` (MOVCA.L first). Use `shz_dcache_alloc_line` only when all 32 bytes of the line are then written.
* **Check on the SH4 path as well as the host.** `tests/sh4zam/` runs the real SH4 code under qemu: logic traces, `tools/compare_frames.py` against a reference build's frame dumps, and instruction counts (`tests/sh4zam/README.md`).

## 11. The 3D battle (slice 3D-4)

Read `docs/SLICE_3D_BATTLE.md` first. The battle's rules are untouched by the presentation: the
state machine (`src/battle.cpp`) pushes events, `src/battle3d.cpp` turns them into sequences and says
when it is busy. Rule changes go in `skills.cpp` / `ai.cpp` / `status.cpp` as before.

**Run and look at a battle on the host:**
```sh
make host
./build/battle_trace_f32 - /tmp/t.trace --battle-test test_slimes --state-only --bot cycle:AFG --bot-wait 120 \
    --view --dump /tmp/frames --dump-every 5                     # the bot plays; every 5th frame dumped
python3 tools/battle3d/review.py /tmp/t.trace /tmp/frames /tmp/review.jpg   # one row per sequence
python3 tools/render_frame.py /tmp/frames/frame_00300.txt romdisk /tmp/f.png
```
`--bot` policies: `attack:K`, `fire`, `guard`, `cycle:SEQ` (`A F G Y P 1 2 3 b c`), `random:SEED`,
`boss:good|naive|noguard:SEED` (`platform/host/battle_bot.cpp`). `--view2d` plays the old sprite
battle, `--rules legacy` switches the slice's rule additions off.

**Add a sequence for a new skill.** In `battle3d.cpp`: write `build_<skill>(Seq&, View& user, ...)`
that adds cues - `cue(s, tick, OP_..., actor, a, b, f, point)` - and dispatch it in `start_seq()` on
`ev.skill`. Useful ops: `OP_PLAY` (a clip, fade, then), `OP_MOVE` + `OP_FACE*` (running), `OP_CAM`
(a shot), `OP_IMPACT k` (hit k of the event lands: shown HP, number, flash, reaction, death),
`OP_LAUNCH k` (a projectile; its arrival is the impact), `OP_BURST`, `OP_END`. Take timings from the
clip: `anim::clip_event(lib, clip, anim::EV_IMPACT)` and `anim::clip_ticks()`. The sequence ends when
`OP_END` has fired, nobody is still moving, no projectile flies and every actor it killed has hit
the ground. Keep everything that decides *when* it ends in integer ticks (the rules wait for it).

**Add an enemy look.** Add a `LOOK_*` (`encounters.h`) and a `K_*` row in `KINDS[]`
(`battle3d.cpp`): model, scale, weapons per hand slot, clip names, ranged or not, head height for
the HUD. Set the look in the encounter table. A new KayKit character: convert it (below), check it
with `qc_model.py`, add it to `build_assets.sh`.

**Convert a character or a weapon** (Blender's Python, see §9):
```sh
$BPY tools/battle3d/convert_model.py --fbx ../KayKit/Characters/Skeleton_Rogue.fbx \
    --texture ../KayKit/Characters/skeleton_texture.png --tris 800 --glow-color 9CF0FF --out romdisk/models/skel_rogue.dmf
$BPY tools/battle3d/convert_model.py --weapon --fbx ../KayKit/Weapons/Skeleton_Blade.fbx \
    --texture ../KayKit/Characters/skeleton_texture.png --tris 90 --out romdisk/models/w_skel_blade.dmf
# QC against a full-resolution conversion of the same FBX (--tris 100000 --dissolve 0 --tex-size 256):
python3 tools/battle3d/qc_model.py /tmp/ref.dmf romdisk/models/skel_rogue.dmf --sheet /tmp/qc.jpg
```
Weapons are drawn with their holder's texture (the KayKit weapons share their character's atlas),
so they cost no VRAM. Attach them in `KINDS[]` (`handslot.r` / `handslot.l`).

**Add or change a clip.** Add a row to `CLIPS` in `tools/battle3d/build_anims.py` (library name, pack,
action, loop, event rule, optional event override) and rebuild (`build_assets.sh`). Check it with
`python3 tools/battle3d/clip_sheet.py romdisk/models/knight.dmf /tmp/c.jpg CLIPNAME --weapon
handslot.r=romdisk/models/w_sword.dmf` (event frames are marked).

**Edit an arena.** `romdisk/arenas/<name>.txt` (format at the top of `village.txt`): props, light,
sky, ground, hero and enemy slots (left to right on screen!), camera shots. An encounter picks its
arena (`EncounterDef.arena`). Keep the worst frame within budget: `tests/battle3d/run.sh` checks
<= 14,000 submitted vertices; `B` lines (`--view`) break a frame down (chars, weapons, ground, props,
fx). If the battle cameras change, rebuild the view-culled houses (`VIEW_CULL=1 build_assets.sh`).

**The boss.** `ai.cpp`: the rotation tables and overrides (GDD App. C). Stats in `encounters.cpp`,
Soul Rend's power in `skills.cpp`. After any change: `tests/battle3d/boss_balance.sh 100` (targets
in `docs/SLICE_3D_BATTLE.md` §3), `python3 tools/battle3d/boss_tune.py` to search, and
`python3 tests/battle3d/check_rules.py` over fresh logs (update its model if the rules changed).

**Tests.** `tests/battle3d/run.sh` (`QUICK=1`, `BPY=`, `SH4=1`). If the rules change on purpose for
the legacy encounters, the baselines no longer apply: they came from the pre-3D build
(`tests/battle3d/record_baseline.sh`), and the equivalence test runs with `--rules legacy`.

## 12. Dialog, scripts and the quest (slice 3D-5)

Full reference: `docs/SLICE_DIALOG.md` and the plan `docs/NEXT_SLICE_DIALOG.md` §7. After any change run
`python3 tools/dialog/lint.py` (seconds) and `tests/dialog/run.sh`.

### 12.1 Add an NPC who talks

1. A model: convert a KayKit character like Wren (`tools/dialog/build_assets.sh`:
   `convert_model.py --fbx .../X.fbx --texture .../x_texture.png --tris 1000 --out assets/vs1_art/bodies/x.dmf`), check it
   with `tools/battle3d/qc_model.py` against a `--tris 100000 --dissolve 0` conversion, then give it VS1's head (§13:
   `tools/vs1art/import_chars.py <vs1 model> romdisk/models/x.dmf --body assets/vs1_art/bodies/x.dmf`). Keep each NPC
   near 2,000 vertices and check the frame cost with `--sweep` (`docs/SLICE_VS1_ART.md` §6). Mind `MAX_CHAR_VERTS`
   and `LIT_POOL_VERTS` in `src/field3d.cpp`.
2. A look: add it to `LOOKS[]` (`src/field3d.cpp`, `library = true`: it animates with the shared library's `idle_a`,
   `walk`, `interact`, `use_item`), `EntityLook` / `look_by_name()` (`src/field.*`). Mind `LIT_POOL_VERTS` (16 yaw
   buckets of baked lighting per look) and the 32 texture slots.
3. The map: `entity smith2 npc <look> 20 15 down talk smith2_main`.
4. A speaker: `@speaker smith2 "Name" RRGGBB smith2` in `romdisk/scripts/speakers.txt` (max 8 speakers; the portrait
   cache holds `PORTRAIT_CACHE` = 9 files: raise it for a 4th speaker, 32 KiB of RAM each) and its portraits (§12.4).
5. A script `@script smith2_main` in `romdisk/scripts/<map>.txt`, and optionally a marker:
   `@marker smith2 ... !talked_smith2`.

### 12.2 Write a script

```
@flags patrol_a patrol_b skill_fire has_token talked_brom ...     # every flag, once (max 32)
@marker wren ! stage == 0 || stage == 2                          # ! gold (the quest needs you), ... grey (optional)

@script brom_main
@if !talked_brom
  anim brom use_item                        # a gesture (library clip), does not block
  brom: Ho! A knight, and a rusty one.      # a page: speaker[(neutral|happy|worried)]: text, '|' = line break
  emote rowan sweat                         # ! ? ... sweat anger, 60 ticks over the head
  rowan: ...Thank you.
  give potions 3                            # capped at 3
  flag talked_brom
  end
@else
  brom: Need a rest?
  choice "Rest (full heal)" -> rest | cancel "Later." -> bye      # 1-3 options; cancel (B) is the last one
:rest
  fade out 20                               # blocks, box hidden
  heal
  checkpoint rest
  fade in 20
  brom: Good as new.
  end
:bye
  brom: The fire's lit whenever you need it.   # a "no" branch must say how to come back (GDD 7.3; the lint checks)
  end
@end
```

* Pages are wrapped at compile time to 3 lines of 420 px with the real font; a page that does not fit is an error
  (split it into two pages). `sys: text` is a gold, centred system page. `{gold}...{/}`, `{red}`, `{grey}` colour text.
* Conditions: `stage`, `potions`, `max_hp`, flag names, integers 0-255, `+ - == != < <= > >= ! && ||` and parentheses
  (C precedence).
* Commands: `set stage N` (forward only) / `set max_hp N`, `flag` / `unflag`, `give|take potions N`,
  `give skill fireball`, `give token`, `heal`, `checkpoint [c1|c2|rest]`, `objective "text"` (`{patrols}` counts the
  patrols; `""` clears it), `face actor dir|actor`, `anim actor clip`, `emote actor kind`, `wait n`, `fade out|in n`,
  `letterbox on|off`, `goto label`, `call script`, `end`. Labels are global. `cam`, `move`, `battle`, `map` ... are
  "not in this slice" errors.
* Special scripts: `new_game` (runs at a new game, no pages), `after_battle` (after every won battle).
* Errors: the game shows SCRIPT ERROR and the serial log says `script: village.txt line N: ...`; the lint says the
  same and more (unreachable pages, unset flags, dead ends, backwards stage sets).

### 12.3 Add a quest stage

1. Pick the stage number (they only move forward; `Progress.stage` is a byte) and any new flags (`@flags`).
2. Set it where it happens: `set stage 4` in a script (a talk, or `after_battle` for a battle's outcome; an enemy's
   `win <flag>` sets its flag first).
3. Give the stage its objective (`objective "..."`), its markers (`@marker` rules that mention it), a checkpoint if
   the GDD wants one, and the NPCs' lines for it (a `@elif stage == 4` branch in each talk script).
4. Gate what appears with it: `if stage >= 4` on a map entity.
5. A route in `tests/dialog/routes/` that reaches it, with `expect stage=4 ...` (order variations: GDD 17), and add it
   to `ROUTES` in `tests/dialog/run.sh`.

### 12.4 Add or change a portrait

`romdisk/portraits/<base>_<neutral|happy|worried>.dtx`: 32-byte header (`DTX1`, u16 width 128, height 128, format 1 =
RGB565, flags 0, 20 zero bytes) + 128x128 linear RGB565 texels. Rendered from the models by
`tools/dialog/make_portraits.py` (add the character to `CHARS`: its high-detail conversion, gradient, glow, framing;
`tools/dialog/build_assets.sh` makes the conversion). A hand-painted portrait can replace a file as long as it keeps
the format (GDD 8). The lint checks that every speaker has all three.

## 13. The VS1 art (village props, textures, character heads)

`docs/SLICE_VS1_ART.md` explains the choices; `assets/vs1_art/README.md` and `tools/vs1art/README.md` list the files.

* **Rebuild** after editing anything in `assets/vs1_art/`: `python3 tools/vs1art/import_props.py` (props + textures),
  `tools/vs1art/build_chars.sh` (characters); `tests/vs1art/run.sh` checks both byte for byte.
* **A new VS1 prop or yaw variant:** build it with VS1's own pipeline (VS1's `tools/dmf/props.py`, in its repository:
  it needs the Medieval Village kit and assimp), copy the `<name>_r<yaw>.dmf` into `assets/vs1_art/models/village/`,
  add the name to `PROPS` in `import_props.py` (and a texture to `TEX_NAMES` if it brings one), rebuild, place it
  with `prop <name> <x> <y> <yaw>`, and add its footprint to `check_layout.py` if it is VS1's.
* **A character with VS1's head:** `python3 tools/vs1art/import_chars.py <vs1 model> romdisk/models/<ours>.dmf
  --body assets/vs1_art/bodies/<ours>.dmf` (the VS1 model: uploaded, or built with `tools/vs1art/vs1_fbx2dmf.py` and
  VS1's recipe, see `build_chars.sh`). The head is every triangle rigid on the `head` bone; KayKit heads are separate
  meshes, so the two halves never share a triangle. Add it to `CAST` in `tests/vs1art/check_chars.py`.
* **Back to our own model** (e.g. if hardware shows a frame over budget): `cp assets/vs1_art/bodies/<name>.dmf
  romdisk/models/` restores the pre-VS1 character exactly (its 64x64 texture included).
* **The ground's look:** the village's ground is painted offline (`docs/GROUND_ART.md`): after changing the map's tiles
  or props run `python3 tools/ground/paint_ground.py` (it writes `romdisk/maps/<map>.gnd`; palette and layers are at the
  top of the script). A map without a `.gnd` gets the generated VS1 ground (`build_ground_texture()` in
  `src/field3d.cpp`; `tests/vs1art/check_ground.py` holds its reference - change both).

## 14. Maps, cutscenes, light and the frame around the game (slice 3D-6)

Full reference: [SLICE_BARROW.md](SLICE_BARROW.md) and the plan [NEXT_SLICE_BARROW.md](NEXT_SLICE_BARROW.md) §5, §7.
After any change run `python3 tools/dialog/lint.py` and `tests/barrow/run.sh`.

### 14.1 Add a map

1. `romdisk/maps/<name>.txt` in the format of §1, then list it in `romdisk/maps/maps.txt` (`map <name>`; the line
   order is the map id in Progress - append, never reorder, or saved ids change meaning). At most 4 maps.
2. Its scripts: `romdisk/scripts/<name>.txt` (compiled at boot with every other file; its scripts run on that map).
3. Its ground: `python3 tools/ground/paint_ground.py romdisk/maps/<name>.txt` (add a palette to `PALETTES` at the top of
   the script, keyed by the map's `name`). Every painted ground must be 512x512 (the maps share one VQ texture slot).
   Without a `.gnd` the map gets the generated VS1 ground (its own slot: 64 KiB of VRAM).
4. Its light: `light <preset>`; optionally `trees <model>` and `fences <model>` for its `T` tiles and `#` runs.
5. A way in: an `exit` on another map, or a script's `map <name> <x> <y> <dir> [light <preset>]`.

### 14.2 Triggers, exits, shots, glows

```
trigger <id> <x> <y> <w> <h> <script> [once] [if <cond>]      # the hero's feet entering the rectangle runs the script
exit    <id> <x> <y> <w> <h> <map> <tx> <ty> <dir> [if <cond>] # T7 to <map>, arriving on (tx, ty) facing dir + a 1-tile walk-in
shot    <id> <eye x y z> <look x y z> <fov>                    # a camera for `cam`
glow    <x> <y> <radius> <rrggbb> [ring] [if <cond>]           # a glowing decal (exit glow, boss ring)
entity  <id> enemy <look> <x> <y> <dir> <encounter> asleep ... # no patrol, no contact: a script starts its battle
```

* A trigger fires on entering (an edge) and re-arms when the hero has left; `once` remembers it in Progress (per map).
  Its script runs as a cutscene (no NPC: no two-shot); to push the hero out, `move rowan -1 0`.
* A closed exit (its `if` false) blocks like the map's edge. Arrival tiles may lie inside another exit or trigger: both
  only fire on entering.
* Conditions are compiled by the script compiler (the lint checks them and that every flag they read is set).

### 14.3 Write a cutscene

```
@map barrow                       # in a file that is not a map's own: where its scripts start
@script my_scene skip             # `skip`: Start fast-forwards it (never through a choice)
  letterbox on
  cam crypt 0                     # a shot of the map: 0 = a cut; `cam <shot> 90` eases over 90 ticks (blocks); `cam field`
  move rowan 12 10 | face wren rowan     # parallel: move, anim, face, emote, cam, wait (at most 4); the line ends when all have
  move rowan -1 0                 # signed numbers: relative (walks one tile back); `run` at the end walks faster
  place wren 9 9 right            # instant (under a fade)
  namecard "THE BONE WARDEN"      # 120 ticks; `card "THE END"` 150 ticks with the logo
  battle warden boss              # T5 into the battle with that map entity; a win goes on here (boss: VICTORY without A, T10);
                                  # a loss shows the death screen (the retry restores the last checkpoint)
  map village 16 10 left light morning   # T7; the following lines run on that map (its actors, its shots)
  light lerp night dawn 240       # or `light <preset>` (at once: best under a fade)
  transition T11                  # any catalogue transition; credits / results / title: the ending's screens
  end
```

* Actors are the map's entities (and `rowan`); the compiler rejects an actor, a shot or a battle of another map.
* A script the game starts by itself (a trigger, the intro) has no NPC: pages show without the two-shot.
* The game's special scripts: `new_game` (the harness's start), `intro` (after NEW GAME), `after_battle` (after a
  contact battle's win), `warden_retry` (the retry after losing to the boss).

### 14.4 Add or change a light preset

`romdisk/maps/lights.txt`: `light <name> dir x y z ambient a diffuse d colour rrggbb tint rrggbb clear rrggbb|ground
[fog rrggbb start end]` (at most 8; `day` is built in and may be redefined). `colour` multiplies the characters and the
runtime-lit props, `tint` the baked (PRELIT) props, the ground and nothing else; check the look with
`tools/render_frame.py` on a `--dump` frame (it emulates the fog). A preset change re-bakes every character (~70 ms):
use `light` under a fade, `light lerp` in the open.

### 14.5 The title, the credits, the results, the demo

* The logo: `python3 tools/make_logo.py` (a real 256x64 ARGB4444 logo can replace `romdisk/tex/logo.tex`).
* The title's orbit and the credits' flyover: `title_orbit()` / `credits_flyover()` in `src/screens.cpp`; after a change,
  `battle_trace_f32 - /dev/null --path title` (or `credits`) prints every frame's cost (`tests/barrow/run.sh` checks it).
* The credits: `romdisk/scripts/credits.txt` (`# heading`, a line, a blank line = a gap, `;` a comment).
* The rank: `results_rank()` in `src/screens.cpp` (decision D8).
* The attract demo: edit `tests/barrow/routes/attract.route`, then `tools/make_attract.sh` (needs `make host`); the
  demo must replay its recording tick for tick (`tests/barrow/run.sh` checks it) - anything that changes the game's
  logic before the demo's end (the village, its scripts, the battle rules) needs a re-recording.

### 14.6 Test a route from the title

`battle_trace_f32 - out.trace --state-only --boot --route my.route` starts as the Dreamcast does (the title). Route steps
for the frame: `newgame`, `title_credits`, `skip`, `death retry|title`, `until <mode>`, `exit <id>`; expectations
`map=`, `cleared=`, `walkable:x,y=`, `transition=`. The trace's `T` lines give each transition's start and end tick,
`R` the results card, `A` the audio hooks; `tests/barrow/check_flow.py` and `check_results.py` check them.

## 15. The UI font and icons (the UI art pass, docs/UI_ART.md)

Everything the 3D game writes outside the dialog boxes (HUD, battle numbers and labels, prompts, banners, screen
titles) uses the UI atlas `romdisk/ui/ui.uia`: `ui_text(UF_S|UF_L, x, y, text, argb, z, scale, align)` with `y` = the
top of the capitals (S: 14 px, L: 28 px), `ui_text_px(x, y, text, cap_px, ...)` to ask for a size, `ui_icon(UI_...,
cx, cy, size, argb, z)`. Do not use `font_draw` / `icon_draw` (the 2D battle view's; `tests/ui/run.sh` fails).
To change a font size or add an icon: `tools/ui/make_ui_atlas.py` (`FONTS`, `L_CHARS`, `ICONS` + `enum UiIcon` in the
same order, `icon_art()`), rebuild, run `tests/ui/run.sh`. A texture drawn with many sprites per frame reserves its
KOS queues at boot (`eng::sprite_reserve`); `--sprite-queues` on the host shows each texture's peak.
