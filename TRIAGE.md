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
| `3D: arena peak A/16384` | the per-frame vertex arena; the village tour peaks at ~13.9k. When it is full, props are dropped first (they draw last), never the hero |
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
