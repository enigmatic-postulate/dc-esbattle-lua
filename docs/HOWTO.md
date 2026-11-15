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
entity guard npc guard 10 16 down guard_duel
```

| Char | Tile | Walkable |
|---|---|---|
| `.` | grass | yes |
| `,` | flowers | yes |
| `=` | path | yes |
| `#` | wall | no |
| `T` | tree | no |
| `~` | water (animated) | no |

* The loader rejects bad files with a line number: unknown tile char, wrong row width, spawn or entity on a blocking tile, unknown encounter. The message goes to the serial log, and the game shows **MAP LOAD ERROR**.
* The field is drawn in 3D (`src/field3d.cpp`, see §8): each tile is a 1×1 unit quad of the ground plane, textured with its cell of `romdisk/tex/ground.png`. The logic still works in 32 px tiles (1/16 px positions). Maps can also place 3D props (§8).
* Movement is free and analog (stick, or the d-pad as a full-tilt stick): up to 24 sub-px/tick (1.5 px/tick = 2.81 tiles/s), the same in every direction. The hero collides with a 16×12 "feet" box (`HERO_BOX_*` in `src/field.h`), so 1-tile corridors are easy to walk, and he slides along walls. Entities block with a 24×20 body box; touching one starts its encounter.
* After editing the test map, re-check the collision sweep `field_walk` (its route is written for this map).

## 2. Add an enemy or NPC to a map

`entity <id> <enemy|npc> <hero|guard|slime> <x> <y> <facing> <encounter> [patrol <dx> <dy> <len>]`

* `id`: unique per map, at most 15 characters. It appears in test traces.
* `encounter`: must exist in `src/encounters.cpp` (see §3). Touching the entity starts that battle. This applies to **NPCs too**, per the slice brief.
* `patrol 1 0 3`: walks back and forth 3 tiles along +x, waiting 45 ticks between steps. If it walks into the hero, the battle starts as well. Exactly one of dx/dy must be non-zero.
* Limit: 16 entities per map (`MAX_ENTITIES` in `src/field.h`).
* A won battle removes the entity for the session. Game Over restores every entity.

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
| `tests/run_tests.sh --update` | re-records `tests/golden/*.trace.gz` after an **intentional** change; review the diff first |
| `python3 tests/check_invariants.py <trace>` | field rules on any trace (e.g. long fuzz runs) |
| `python3 harness/gen_inputs.py field_fuzz ms60 <seed> 6000 x.in` | new random field session |

Input scripts are one line per 60 Hz tick: `<dt> <buttons-hex> [<stick-x> <stick-y>]` (dt is ignored; the stick columns are optional, -128..127, + = right/down). Bits: up 1, down 2, left 4, right 8, A 16, B 32, Y 64, Start 128; combine bits for diagonals (up+right = 09). Walking straight at full tilt covers one tile per ~21 ticks.

The 3D slice adds `tests/slice3d/run.sh` (see `docs/SLICE_3D_1.md` §6): runtime vs reference, validator cross-check, sanitizer fuzz, movement invariants, float == double, battle traces vs a baseline (`BASELINE=<dir>`), and SH4 runs under qemu (`SH4=1`).

**Building for the Dreamcast.** The makefile tracks header dependencies (`.d` files) since the VS2 fix-up. Run `make clean` once after updating from an older tree, and whenever you change a build flag (`DIAG=1`, `R3D_SAFE=1`, `PVR_DEFAULTS=1`, `SHZ_DEBUG=1`, `HOT_OPT=`). Every build prints `[boot]` breadcrumbs on the serial log; `TRIAGE.md` (last section) lists the expected sequence.

**SH4ZAM (required since the SH4ZAM slice).** The 3D math runs on the SH4ZAM library; see §10. Install its KallistiOS port once (DreamSDK Manager > KallistiOS Ports > SH4ZAM; or `cd $KOS_PORTS/sh4zam && make install`), or build against a checkout with `make SH4ZAM_DIR=<path>`. The host build (`make host`, `tests/run_tests.sh`) needs a checkout and CMake: it finds `SH4ZAM_DIR`, `external/sh4zam` or `../sh4zam/sh4zam`.

**Battle controls.** Command ring: ◀▶ choose, A select, Y jumps to Guard. Attack → pick a target (◀▶, A), B back. Fire → confirm step with every enemy marked (A cast), B back. Guard casts immediately.

## 8. The 3D village (field)

The boot map is `romdisk/maps/village.txt`. `test_field.txt` still loads with `--map` on the host.

Besides the tile rows, `spawn` and `entity` lines, a map can place 3D props:

```
prop <model> <x> <y> <rot_deg> [solid <w> <h>]
prop house_stone   8.0  10.0    0 solid 4 4     # door faces south (rot 0); blocks a 4x4 footprint
prop house_stone   9.0  19.0  180 solid 4 4     # door faces north
prop crate         5.5  12.5    0 solid 1 1
prop bush          5.4   9.2    0               # decoration: does not block
```

**Placement:**
* `model` = `romdisk/models/<model>.dmf` (name up to 15 characters). If `<model>_r<yaw>.dmf` exists for the prop's yaw, it is used instead: the same model with the faces the camera can never see at that yaw removed. `tools/dmf/build_models.sh` makes them for every house yaw in `village.txt`. A house at a new yaw works without a rebuild; it is just more expensive until you rebuild.
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
* Houses and trees that cover the hero on screen fade to `fade_alpha`: front faces only, TR list.
* Static props have their lighting baked once per instance.
* Enemies and the hero use lighting baked per yaw (`hero_dyn_light 1` lights the hero per vertex instead).
* The village's frame cost depends on the geometry: see `docs/SLICE_3D_2.md` §4 before adding many props in one view.

**Adding a prop:**
1. Add an entry to `PREFABS` in `tools/dmf/build_village.py`. Each entry lists the kit parts with position, rotation and scale. Buildings use `view=True, per_yaw=True, simplify=True` and `decimate` for the parts that may be collapse-decimated (roof tiles, chimney rocks); small props use a triangle `budget`. See the docstring of `build_village.py`.
2. Rebuild with `tools/dmf/build_models.sh`.
3. Place it with `prop` lines.

Check the result with `preview_dmf.py --texdir romdisk` and a host frame dump.

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
