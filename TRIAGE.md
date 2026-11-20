# Triage: `battle.elf` crashes immediately in Flycast

## What's already ruled out (checked here, without Dreamcast hardware)

| Suspect | Verdict | Evidence |
|---|---|---|
| Memory error / undefined behaviour in the ported game code | ruled out on host | ASan + UBSan (incl. `alignment`, `float-cast-overflow`) across all 138 suite runs / 403k frames: clean |
| Logic divergence from the Lua build | ruled out | trace-equivalent to the original scripts (REPORT.md §6) |
| Misaligned PVR submissions (SH4 faults on misaligned access; x86 doesn't) | ruled out | your KOS declares `pvr_vertex_t`/`pvr_poly_hdr_t` `aligned(32)` (read from `zip/main.o` DWARF); GCC 13.2 realigns stack locals (disassembly of `rect_flush_list`); and your `pvr_prim` *rejects* misaligned or unopened-list data with a log line (`attempt to submit data unaligned to 8 bytes`), no crash |
| Romdisk not mounted | unlikely | same `KOS_ROMDISK_DIR` + `__kos_romdisk` auto-mount as the original ELF; a failed PNG load returns handle 0 and draws nothing, no crash |
| C++ static-initialisation order | unlikely | every static in `src/` is zero- or constant-initialised |
| newlib `rand()` | removed | KOS backend now uses the same LCG as the verified host build |

The per-frame PVR traffic is the same renderer code as `zip/main.cpp`, fed the identical call sequence. So the next step needs the **crash location**, which only your machine can produce.

## Step 0: check the environment baseline (2 min)
Rebuild and run the **original** `zip/` project with the same DreamSDK and Flycast. If it also crashes now, the problem is environmental (SDK update, Flycast version/settings, BIOS/HLE), not the port.

## Step 1: capture the serial log
KOS writes breadcrumbs and crash dumps to SCIF (serial). In Flycast, turn on **Serial Console** (Settings → Advanced) and launch Flycast from a terminal, or enable logging to file. The log needs to include everything from `=== DREAMCAST BOOT (C++ battle) ===` onward.

## Step 2: build the diagnostic variants (always `make clean` between variants; make does not track flag changes)
```sh
make clean && make DIAG=1                    # A: full game + breadcrumbs
make clean && make DIAG=1 DIAG_SKIP=game     # B: renderer + input overlay only, no game code at all
make clean && make DIAG=1 DIAG_SKIP=update   # C: game_init + game_draw, never game_tick
make clean && make DIAG=1 DIAG_SKIP=draw     # D: game_init + game_tick, never game_draw
```

| Result | Meaning | Next |
|---|---|---|
| **B crashes** | platform layer / build / environment; not the ported game code | compare against step 0; send log |
| B runs, **A crashes** | game code on SH4 | C and D narrow it to tick or draw; the last `[diag]` line shows the stage |
| Log has `Unhandled exception: PC …` | CPU exception | `python3 tools/symbolize_crash.py battle.elf flycast.log` → function, file:line, disassembly, caller |
| Log just stops, no exception line | hang or emulator-side crash | check Flycast's own log for `pvr_irq: ... TA_INPUT_ERR / OUTOFMEM` |
| Last line is `sprite_load(...)` / `png ok` / `vram=` | asset load/upload | note the dimensions printed; `es_sprite_64.png` is 1024×1024 (2 MiB VRAM) |

`DIAG=1` also prints:
* struct sizes at boot;
* every sprite load (dimensions, format, running VRAM total);
* stage markers for frames 1–3;
* every 300 frames, a report line: `worst CPU frame … us, frames over 16.7 ms: …, heap in use … B (+0 since last report)`. These are slice DoD T2 and T3; after warm-up the heap delta should be 0.

The field map loads from `/rd/maps/test_field.txt`. A broken map shows **MAP LOAD ERROR** on screen, with the line number in the serial log.

## Step 3: send back
* the Flycast/serial log from variant A (and B if A crashes)
* the `battle.elf` that produced it (for symbolising and disassembly on this side)
* the exact `make` command and the DreamSDK/KOS version

---

# 3D field (3D slice 1): what to look at if the field misbehaves

`DIAG=1` adds these serial lines:

| Line | Meaning |
|---|---|
| `FOUND: /rd/models/knight.dmf` (and `cube`, `sphere`) | the romdisk has the models. `MISSING` = rebuild the romdisk (`make clean && make`) |
| `dmf: /rd/models/knight.dmf ok: 1611 verts, 23 bones, 2 anims, 80960 bytes` | the file passed validation |
| `dmf: ... rejected: <reason>` | the file is corrupt or from a newer converter; it is never drawn |
| `texture_create(64x64 fmt 1) slot N` / `material N: tex T flags F list L` | texture upload and PVR header compile |
| `3D: arena peak A/8192 verts, drops D; PVR vtx buffer peak B, F fps` | per 300 frames. `drops > 0` = a batch didn't fit the vertex arena. F should read 60.0 |

| Symptom | Likely cause | Next step |
|---|---|---|
| Field is the clear colour only (dark green); HUD visible | 3D batches not reaching the TA | `make clean && make R3D_SAFE=1 DIAG=1`: submits 3D strips one vertex per `pvr_prim()`, exactly like the 2D sprites. If that draws, the bulk `pvr_prim` path is at fault: send the log |
| Ground draws, the Knight doesn't | `dmf:` line says rejected/missing, or the model is culled | check the `dmf:` lines. The Knight is never near the near plane with the default camera |
| The Knight looks inside-out | back-face culling was turned on (`MAT_CULL`) with the wrong winding | culling is off by default. If you turned it on, swap `PVR_CULLING_CW` ↔ `PVR_CULLING_CCW` in `material_create` (`platform/kos/main.cpp`) |
| Garbled or spiky polygons | bad vertex data reaching the TA | host-render the same tick (`--dump`, `tools/render_frame.py`). If the host frame is fine, suspect the submission: try `R3D_SAFE=1` |
| Frame rate below 60 | CPU over budget (`worst CPU frame` > 16.7 ms) or TA/ISP overload | compare `worst CPU frame` with the ~3–4.5 ms estimate (`docs/SLICE_3D_1.md` §5). `tests/slice3d/sh4_cost.sh` profiles the SH4 code by function |
| Stick does nothing / drifts | controller not reporting the analog axes, or dead zone | the debug overlay's red dot shows the raw stick. `STICK_DEAD` / `STICK_FULL` are in `src/field.h` |

---

# Village (3D slice 2): additional checks

| Line / symptom | Meaning / next step |
|---|---|
| `field3d: 180 static instances, N prelit colour sets, ...` | the village was built. A `prop model 'x' missing` line names a `.dmf` that isn't on the romdisk |
| `dmf: /rd/models/house_stone_r0.dmf ok` (and `_r180`, `house_plaster_*`) | the houses load their per-yaw view-culled models. A plain `house_stone.dmf` line means a house sits at a yaw without its own model: correct, just more vertices; rebuild the models (`tools/dmf/build_models.sh`) |
| A house has a hole or a missing piece from some camera position | the view culling removed a face it should not have. Run `tests/slice3d/check_views.py` on the `--reference` output of `build_village.py`, and send the hero position (the `V cam=` line of a host `--view` trace) |
| `dmf: shared texture /rd/tex/x.png failed to load` | a texture referenced by a model is missing from `romdisk/tex/`: rebuild with `tools/dmf/build_models.sh` |
| `material limit reached` | more than 128 PVR materials (the village uses ~100: houses x LOD x faded copies) |
| `3D: arena peak A/20480` | the per-frame vertex arena (20,480 since slice 3D-5; it was 16,384 and the village already peaked at 15.8k). The village tour peaks at ~16k. When it is full, props are dropped first (they draw last), never the hero |
| `PVR vtx buffer peak B` | TA vertex buffer use. This build asks for 1 MiB (`pvr_init` in `main()`, otherwise identical to `pvr_init_defaults`). If B approaches 1 MiB, polygons are lost: reduce the props in view |
| Everything renders but the screen is black/garbage right at boot | `pvr_init` with the custom parameters is the only change to the PVR init path: `make clean && make PVR_DEFAULTS=1` builds with `pvr_init_defaults()` to bisect (the village then needs the vertex buffer to stay under 512 KiB) |
| A house looks inside-out while faded | the faded (translucent) house materials cull back faces, the only culled materials in the build. Swap `PVR_CULLING_CW` / `PVR_CULLING_CCW` in `material_create`, or remove `MAT_CULL` in `make_faded` (`src/field3d.cpp`) |
| 30 fps instead of 60 in busy views | expected if the CPU frame exceeds 16.7 ms: the game logic still runs at 60 ticks/s and the display drops to 30. Compare `worst CPU frame` with `docs/SLICE_3D_2.md` §4; the SH4 assembly slice targets exactly these loops |

## Boot stops after the `FOUND:` lines (VS2 report, KOS 2.3.0 / Flycast)

Every build now prints `[boot]` breadcrumbs: always on, sent through the same synchronous
`dbgio` path as the `FOUND:` lines (flushed per line, not filtered by `dbglog`'s level), with
milliseconds since boot. The `dmf: ... ok` lines and map errors use that path too now. A healthy
boot looks like this (about 40 lines, then nothing more after frame 600):

```
=== DREAMCAST BOOT (C++ field + battle) ===
[boot     0 ms] pvr up: TA vertex buffer 1024 KiB, texture VRAM free ~4500 KiB
FOUND:   /rd/maps/village.txt            (... 10 FOUND lines ...)
[boot    .. ms] game_init begin
[boot    .. ms] 2D assets (icons, font, battle sprites)
[boot    .. ms] png /rd/ui_icons.png      (then font, hero, guard, slime)
[boot    .. ms] field_load
[boot    .. ms] field_load: parsing
[boot    .. ms] field_load: ok, 36x28, 24 props, 3 entities
[boot    .. ms] field3d_init
[boot    .. ms] /rd/models/knight.dmf
dmf: /rd/models/knight.dmf ok: 1611 verts, 23 bones, 3 anims, 85952 bytes
[boot    .. ms] /rd/models/skel_warrior.dmf      (... skel_mage, fence + png /rd/tex/wood.png, tree ...)
[boot    .. ms] field3d: characters loaded and lit; ground texture
[boot    .. ms] png /rd/tex/ground.png
[boot    .. ms] field3d: building the static scene (props, fences, trees)
[boot    .. ms] /rd/models/house_stone.dmf      (... 10 more models and their png lines ...)
field3d: 180 static instances, 30 prelit colour sets, 12217/49152 colours, 15 models, 465 KiB model arena
[boot    .. ms] game_init: field ready
[boot    .. ms] game_init done: 20 textures, ... KiB texture VRAM used, ... KiB free, heap ... KiB
[boot    .. ms] frame 1: 0 tick(s) done
[boot    .. ms] frame 1: PVR ready
[boot    .. ms] frame 1: drawn (N 3D verts in B batches, R rects)
[boot    .. ms] frame 1: lists submitted
[boot    .. ms] frame 1: scene finished            (same 5 lines for frame 2)
[boot    .. ms] frame 60: running, 60.0 fps, TA vertex buffer peak ... KiB of 1024
[boot    .. ms] frame 600: running, ...
```

The last line printed names the stage that stalled:

| Last line | Stalled in | Next step |
|---|---|---|
| `pvr up` / a `FOUND:` line | before `game_init` (should be impossible) | send the log |
| `png <path>` | KOS `png_to_img` / `pvr_txr_load_kimg` for that file | send the log (all PNGs are RGBA now, the format the 2D build always loaded) |
| `field_load: parsing` | the map parser | send `romdisk/maps/village.txt` as built |
| `/rd/models/<name>.dmf` without its `dmf: ... ok` line | reading/validating/uploading that model | send the log |
| `field3d: characters loaded and lit; ...` / `building the static scene` | lighting bake / scene build (pure CPU: all of game_init except PNG decoding is ~20 M SH4 instructions under qemu, i.e. ~0.1-0.2 s) | send the log |
| `frame 1: 0 tick(s) done` | `pvr_wait_ready()`: the PVR never finished a previous render | `make clean && make PVR_DEFAULTS=1`, then `R3D_SAFE=1` |
| `frame 1: drawn` / `lists submitted` | TA submission / `pvr_scene_finish()` | as above; note the vertex/batch counts in the `drawn` line |
| frames fine, then nothing | a later hang; `make clean && make DIAG=1` adds the 300-frame reports | send the log |

What changed after the report (VS2 fix-up):
* **Build hygiene.** The makefile had no header dependencies, so a plain `make` over an older
  tree kept objects compiled against old headers. `src/game.cpp` did not change in VS2 but
  includes `src/field.h`, whose `FieldState` grew by ~10 KB: a stale `game.o` reads the
  entities at the wrong offsets. The makefile now generates `.d` files (`-MMD -MP`); **run
  `make clean` once** (the first build after this update has no `.d` files yet). Prebuilt
  `*.o`, `romdisk.img/.o` and `battle.elf` from the original upload were also removed from the
  tree (they are build products; `.gitignore` now lists them).
* **No `%f` scanf in the map loader.** `prop` lines are parsed by hand (integer + fraction
  digits, no `strtod`); `tests/slice3d/check_map_props.py` checks it against Python's
  `float()` and checks that 9 kinds of malformed lines are rejected.
* **No function-local static with a constructor** in `field_load` (it was the only one in the
  game and went through libstdc++'s `__cxa_guard_acquire` → `kthread_once` + mutex); it is a
  file-scope static now.
* **Textures are RGBA PNGs now.** VS2's `romdisk/tex/*.png` were 8-bit RGB (PNG colour type
  2), a format KOS's `png_to_img` had never been given by this game; every 2D PNG is RGBA, and
  `tiles.png` went through exactly the RGBA → RGB565 path the opaque textures use. Same texels
  (checked), alpha 255; `tools/dmf/build_village.py` writes RGBA from now on.
* **Fixed:** `house_plaster_lod.dmf` was loaded three times (a 16-char name buffer truncated
  the 17-char name, so the model cache never matched it): 62 KiB of model arena, 2 model slots
  and their PVR materials wasted. Arena use is 465 of 640 KiB now. Nothing draws differently.
* Checked and **not** changed: the 1 MiB TA vertex buffer. KOS 2.3's `pvr_allocate_buffers`
  puts each of the two buffer sets in its own 4 MiB bank and derives the texture base from
  what it used (~1.8 MiB per bank with 1 MiB vertices), so it cannot overlap textures; `pvr_init`
  has returned before the `FOUND:` lines are printed. `make PVR_DEFAULTS=1` switches back to
  `pvr_init_defaults()` for an A/B test.

---

# 3D battle (slice 3D-4): what to look at if the battle misbehaves

Serial lines (always on, from `dbg_print` / `boot_log`):

| Line | Meaning |
|---|---|
| `FOUND: /rd/anims/rig_medium.dma`, `/rd/arenas/village.txt`, `/rd/arenas/barrow.txt`, `/rd/tex/fx.tex` ... | the romdisk has the battle's files. `MISSING` = rebuild the romdisk (`make clean && make`) |
| `anim: /rd/anims/rig_medium.dma ok: 28 clips, 23 bones, 258112 bytes` | the clip library passed validation |
| `anim: ... rejected: <reason>` | corrupt or from a newer builder: the battle still plays (the rules never need it) but nothing animates |
| `arena village: 20 props (...)`, `arena barrow: 23 props (...)` | both arenas parsed; `arena ...:N: bad line` names the line |
| `battle3d: ready=1 (library 1, arena 1), model arena N KiB` | everything the battle needs is loaded. `ready=0`: see the lines above |
| `material limit reached` / `sprite limit reached` | the KOS backend's tables are full (256 materials, 32 textures; the game uses 124 and 19) |

| Symptom | Likely cause | Next step |
|---|---|---|
| The battle hangs on an action (nothing moves, no menu) | a sequence never ends, so the rules keep waiting (`battle3d_busy()`) | host-replay the same inputs and read the `B` lines (`--view`): `seq=` / `t=` show the stuck sequence. The host suite (`tests/battle3d/run.sh`) plays hundreds of battles to the end; report the inputs |
| Glows, sparks, fire and the selection rings are dark or opaque squares | additive blending not applied (`MAT_ADDITIVE`) | `material_create` in `platform/kos/main.cpp` sets `cxt.blend.src = PVR_BLEND_SRCALPHA; cxt.blend.dst = PVR_BLEND_ONE` on a TR material. Check the KOS version's `pvr_poly_cxt_t`; as a stopgap drop `MAT_ADDITIVE` from `g_fx_add` in `battle3d_draw.cpp` (plain alpha blending, dimmer glows) |
| Skeletons flash as solid white silhouettes for long | the hit flash (an untextured white copy, 2 frames) stuck | it lasts `flash` ticks (`battle3d.cpp` `impact()`); host-render the frames around the hit |
| Parts of the village houses missing in the battle | the view-culled `house_*_b.dmf` don't cover a camera the game now uses | rebuild them (`VIEW_CULL=1 tools/battle3d/build_assets.sh`), or point `romdisk/arenas/village.txt` back at `house_*_r0` |
| `drops > 0` in the DIAG frame report during battles | the vertex arena (20,480 since slice 3D-5) is full | the worst battle frame is 13.9k on the host; compare the `B` lines' `out=` (`tests/battle3d/run.sh` budget check) |
| Frame rate below 60 in battle | CPU over budget | the SH4 build measured 0.98 M instructions median, 1.34 M worst (`tests/battle3d/sh4_budget.sh`), as the field's; `DIAG=1`'s `worst CPU frame` should stay below ~9 ms. Otherwise send the DIAG log with the battle it came from |

# Dialog, portraits and the quest (slice 3D-5): what to look at if they misbehave

Boot adds 17 `FOUND:` lines (`/rd/fonts/dialog.fnt`, `dialog.tex`, `/rd/scripts/village.txt`, `speakers.txt`,
`/rd/models/wren.dmf`, `brom.dmf`, 9 `/rd/portraits/*.dtx`). Every symptom below can be reproduced on the host first:
`./build/battle_trace_f32 - out.trace --state-only --route <route>` with a route of `talk` / `chase` / `choose` steps
(`platform/host/story.cpp`); the trace's `G` lines show the dialog UI tick by tick, `Q` lines the quest state.

| Symptom | Likely cause | Next step |
|---|---|---|
| **SCRIPT ERROR** instead of the field | the village script or speakers.txt does not compile; the serial log has `script: village.txt line N: <reason>` | `python3 tools/dialog/lint.py` prints the same error (and more) on the host; fix the line |
| **MAP LOAD ERROR** after editing the map | a `npc` without `talk <script>`, an unknown look (`slime guard mage smith`), a bad `if` condition | the `map error line N` log line |
| Lint: "page does not fit the box" | the page wraps to more than 3 lines of 420 px with the real font | split it into two pages (the words can stay) or reword |
| Lint: "is read but never set" / "unreachable" / "dead end" / "never blocks" | a flag nothing sets; a page after an `end`; a loop with no way out; a loop of instant commands | the line number given; the `--dump` output shows the compiled jumps |
| No **A Talk** prompt next to an NPC | not facing it within 60 degrees, farther than 1.3 tiles, the NPC hidden by its `if`, or another NPC nearer | walk straight at it; `battle_trace --probe-talk` lists the target for every position and facing (`tests/dialog/check_talk.py` is the rule) |
| A press does nothing at the prompt | the A was held from before (a press edge is needed: GDD 4), or the script ended at once (no page: e.g. a talk script that only sets flags) | release and press; check the script's first branch with the lint's `--dump` |
| **Dialog stuck** (box up, A does nothing) | a `wait` / `fade` still counting (the box is hidden then), or a choice waiting (A needs a fresh press after it opened) | the `G` lines: `op=` and `pc=` show what the VM waits on; a VM error logs `script: ... script stopped` and closes the dialog |
| The dialog never closes / loops | a script loop; the VM stops a run of 512 commands without a page ("a loop with no exit") | the lint reports loops before they ship |
| **Portraits scrambled** (stripes, blocks) on hardware | the portrait cache is twiddled at load (`src/portrait.cpp`, KOS `TWIDOUT` order) and uploaded with `pvr_txr_load`; a mismatch scrambles it | `make clean && make PORTRAIT_KOS_TWIDDLE=1`: KOS twiddles each upload itself (the path every other texture takes; slower). Report which build looked right |
| Portrait black / missing | its `.dtx` is missing or not 128x128 RGB565 (`portrait: ... missing` at boot) | `tools/dialog/build_assets.sh`; the lint checks every speaker's three files |
| A portrait flickers for 2 frames on an expression change | expected: a slot on screen is hidden 2 frames before it is rewritten (the PVR renders the previous frame while a tick runs) | - |
| Fire greyed with a lock in battle | Fire is learned from Wren (accepting her request, or her report if the patrols were beaten first) | `Q` lines: `flags=skill_fire`; `--rules legacy` / `--skills 1` unlock it in tests |
| The Warden never appears | he appears with Wren's token (`if has_token`): report both patrols to her | `Q` lines: `has_token` |
| GAME OVER -> A restarts somewhere else than expected | it restores the **last checkpoint**: new game (C0), accepting Wren (C1), her report (C2), or Brom's rest | `Q` lines: `cp=<id>@<x>,<y>`; `tests/dialog/check_checkpoints.py` |
| The pad overlay (squares top left) is gone | since slice 3D-5 only in `make DIAG=1` or `make INPUT_OVERLAY=1` builds (it covered the objective HUD) | - |
| Frame drops in a dialog | the two-shot sees more props than the field camera; the DIAG report's worst CPU frame at a dialog vs the field's | `SH4=1 tests/dialog/run.sh` measures it under qemu; `field3d_tune dlg_dist` / `dlg_pitch` |

# The VS1 art (village, characters): what to look at if it misbehaves

[docs/SLICE_VS1_ART.md](docs/SLICE_VS1_ART.md). Boot checks three more files (`/rd/models/house_stone_r0.dmf`,
`wagon_r0.dmf`, `/rd/tex/glass.png`). The VS1 village is 40x30 with no tile walls: its edges block by the map bounds.

| Symptom | Likely cause | Next step |
|---|---|---|
| `dmf: ... rejected: PRELIT on a skinned model` | a character file was written with the PRELIT flag | rebuild with `tools/vs1art/build_chars.sh` (only the props are PRELIT) |
| `dmf: cannot read ... (larger than the N free arena bytes)` at boot | the model arena (1,792 KiB; the game uses ~1,487 KiB) is full: a model was added or grew | `ARENA_BYTES` in `src/dmf.cpp`; the `field3d: ... KiB model arena` line shows the use |
| Houses black or untextured, windows grey | a shared texture failed (`dmf: shared texture /rd/tex/x.png failed to load`) | `python3 tools/vs1art/import_props.py` rewrites `romdisk/tex/` and the props (`--check` compares) |
| Houses lit wrongly (flat grey, too dark) | the PRELIT colours were replaced by runtime lighting of zero normals | `r3d::prelight` must copy the vertex colours for `DMF_PRELIT` models; host-render a frame (`--dump`, `tools/render_frame.py`) |
| The ground is one flat colour / shows tile cells / looks like VS1's pale mint and sand | the painted ground (`/rd/maps/village.gnd`, docs/GROUND_ART.md) did not load: a `field3d: ... is not a GND1 VQ ground for this map` line (the map's size changed: re-run `tools/ground/paint_ground.py`), or `texture_create_vq` failed | the generated VS1 ground is the fallback; `tests/vs1art/check_gnd.py` / `check_ground.py` on a host dump check each bit for bit |
| The ground shows **scrambled 2x2 blocks, stripes or noise** on hardware (fine on the host) | the PVR reads the VQ layout (codebook texel order, index twiddling) differently than `eng::vq_decode` assumes | `make clean && make GROUND_NO_VQ=1`: the same picture decoded at boot and uploaded as plain RGB565 (512 KiB VRAM). Report which build looked right |
| Ground details in the wrong places after editing the map (a shadow with no prop, a road under a house) | the `.gnd` is painted from the map and the props: it is stale | `python3 tools/ground/paint_ground.py` (`--check` tells if it is stale; `tests/vs1art/run.sh` runs it) |
| A dark teal band at the map's side | the outer ground beyond the map (VS1's colour); the camera clamp keeps it to a sliver at the gate and the east end | `field3d_tune cam_clamp_side` (4.5) |
| A prop under the camera flickers or has holes (south houses) | the near-plane path (strip runs of in-front triangles, clipped ones one by one) | `--tune near_strips=0` (host) switches back to the former per-triangle path; `tests/vs1art/run.sh` checks they draw the same pixels |
| Characters: a gap at the neck, a floating head | head (VS1) and body (ours) skinned with different bind poses | `python3 tests/vs1art/check_chars.py` (same skeleton, posed heads compared); rebuild with `tools/vs1art/build_chars.sh` |
| The skeletons' eyes dark instead of glowing cyan | the eye vertices are lit instead of taking their vertex colour | `models_glow_range()` finds them as the untextured batch's vertex range; both `field3d.cpp bake_look` and `battle3d_draw.cpp bake_slice` overwrite them unlit |
| Frame rate below 60 near the road patrol or in its battle | the VS1 heads add ~550-980 vertices per character (the battle's overview of the road squad is the costliest frame, ~17.4k) | per model, `cp assets/vs1_art/bodies/<name>.dmf romdisk/models/` restores the pre-VS1 character exactly; `tools/vs1art/sweep_map.py` maps the field cost per tile |

---

# Slice 3D-6 (the title, the gate, the barrow, the ending): what to look at

Nothing of this slice has run on hardware yet (docs/SLICE_BARROW.md §6). New boot lines (every build): `FOUND:`
`/rd/maps/maps.txt`, `lights.txt`, `barrow.txt`, `barrow.gnd`, `/rd/scripts/barrow.txt`, `ending.txt`, `credits.txt`,
`/rd/models/corwin.dmf`, `waystone.dmf`, `dead_tree.dmf`, `iron_fence.dmf`, `/rd/tex/logo.tex`, 6 new portraits;
`game_boot: title`.

| Symptom | Likely cause | Next step |
|---|---|---|
| "SCRIPT ERROR" at boot | a script, the map table or a map's links failed to compile (every file is compiled at boot) | the serial log's `script: <file> line N: ...`; `python3 tools/dialog/lint.py` says the same on the host |
| The barrow is flat grey / no fog at all / fog in the village | the PVR's table fog (`pvr_fog_table_linear` maps W through the fog density register) differs from the host's linear emulation | `make clean && make NO_FOG=1`: the night reads without fog (tint + clear colour). Report what the fog looked like |
| The barrow's ground garbled (blocks, stripes), the village's fine | the VQ slot reuse (`eng::texture_update_vq`) | `make clean && make GROUND_NO_VQ=1` (decoded at boot, plain RGB565, the same slot reuse) |
| The death screen is black instead of grey-red | `MAT_MULTIPLY` (PVR_BLEND_DESTCOLOR / ZERO) | remove the multiply quad in `trans_draw_tint` (`src/transitions.cpp`): the overlay and the vignette remain |
| A hitch when entering / leaving the barrow, at NEW GAME, on RETRY | the map load under the black: field + scene + re-bake of every character (~70 ms) + the ground upload | expected; it must stay under the fade (the screen is black for the last 2 ticks before the swap) |
| A frame-rate dip during the dawn in the barrow | the light lerp: characters lit per frame, then re-baked 2 yaw buckets per tick | DIAG's worst CPU frame there; the lerp's 8-tick steps relight every prop colour set (the barrow's are small) |
| Start does not skip | only `@script <name> skip` scripts skip (the intro, the boss intro, the ending), never on a choice, never during a transition; the credits skip with Start too | - |
| The demo never starts / ends at once | 40 s (2,400 ticks) without any input on the title; any pad button ends it (a drifting stick counts as input only past 40) | `attract: N runs, T ticks` in the boot log; no `/rd/demo/attract.in` = no demo |
| The demo goes off its path (walks into a wall, misses Wren) | the recorded inputs no longer match the game's logic (a map, script or rule change) | `tools/make_attract.sh` re-records it; `tests/barrow/run.sh` checks the replay |

# Cel-shading test builds (`make CEL=1` / `make CEL=2`, docs/CEL_SHADING_STUDY.md)

A feasibility prototype, not a feature: the default build never draws outlines. `CEL=1` outlines every character
(decimated hull meshes `romdisk/models/hull/*.dmf`, 2 px) and bands the lighting; `CEL=2` outlines the hero only.
Always `make clean` when switching (make does not track the flag).

| Symptom | Cause / fix |
|---|---|
| Characters drawn solid black (or a black shell over them) | the hull's culling is the wrong way round on hardware: in `platform/kos/main.cpp` `material_create`, use `PVR_CULLING_CW` for `MAT_CULL_FRONT` |
| No outlines at all | the hull files did not load: the serial log shows `/rd/models/hull/<name>.dmf` after each character; check they are on the romdisk (`make rm-elf && make`) |
| Black blotches on faces / bodies | the coarse hull pokes through: raise `zpush` (0.05 -> 0.1) in `g_cel` (`src/render3d.cpp`) |
| Missing scenery, `drops` > 0 in DIAG | the vertex arena is full: should not happen with the decimated hulls (host peak 19,047 / 20,480); report the scene |

# The UI art pass (docs/UI_ART.md): what to look at

| Symptom | Cause / fix |
|---|---|
| The HUD and battle text look like the old 5x7 pixel font | the atlas did not load: the serial log says `ui: /rd/ui/ui.uia missing or invalid`; rebuild the romdisk (`make rm-elf && make`) - the game keeps working on the old font (the fallback) |
| Thin lines or specks at the edges of glyphs / icons | bilinear filtering reading a neighbour in the atlas: raise the gap in `pack()` of `tools/ui/make_ui_atlas.py` (2 px today) |
| DIAG's heap line grows during the credits or a busy battle | a sprite queue grew: run the host with `--sprite-queues` on the same route; reserve the texture's queue in its init (`eng::sprite_reserve`) |

## Slice 3D-7: sound (docs/sound_save/STAGE_A_NOTES.md)

| Symptom | Likely cause | What to do |
|---|---|---|
| The serial log stops after `sh4zam ... linked` (Flycast) | KOS's `scif_flush()` timed out and switched serial output off for good | fixed: the log uses its own flush (`log_flush` in `platform/kos/main.cpp`); never call `dbgio_flush()` |
| `make` builds only `romdisk.img` | a rule above `all:` became the default goal | the makefile sets `.DEFAULT_GOAL := all`; `tests/sound_save` checks it |
| No sound at all; boot log says `audio: the sound backend is off` | `snd_boot` failed: the line before it says why (`snd_stream_init_ex failed`, `effect ... did not load`, `no stream`) | send the boot log; `make clean && make NO_MUSIC=1` to test the effects alone |
| No sound; boot log says `audio: no /rd/audio/cues.txt` | the romdisk was not rebuilt with `romdisk/audio/` | `make clean && make`; `python3 tools/audio/make_standins.py` regenerates the files |
| Effects play, no music | the stream or its thread | the DIAG report's `audio:` line: `tracks started` stays 0 = the request never reached the thread; `poll errors` rising = `snd_stream_poll` fails |
| Music stutters or repeats a second of itself | the stream ran dry: the poll thread did not run for longer than the buffer lasts | the DIAG `longest gap between stream polls` (must stay far below `buffer 1486 ms`); note the scene |
| A click each time a track loops | the loop seam | `python3 tools/audio/adpcm.py romdisk/audio/music/<track>.adp` must print `seam=ok`; if it does, report it (the chip differs from the model) |
| A loud click when a track starts | the start reached the AICA before its volume | report it: the queued start (`snd_stream_queue_enable`) should prevent this |
| The game itself behaves differently with sound | it must not | `make clean && make NO_AUDIO=1` and compare; `tests/sound_save` AU-T7 proves the logic never feeds back |

