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
