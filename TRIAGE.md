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
make clean && make DIAG=1 DIAG_SKIP=game     # B: renderer + input overlay only, no battle code at all
make clean && make DIAG=1 DIAG_SKIP=update   # C: init + draw, never update
make clean && make DIAG=1 DIAG_SKIP=draw     # D: init + update, never draw
```

| Result | Meaning | Next |
|---|---|---|
| **B crashes** | platform layer / build / environment; not the ported game code | compare against step 0; send log |
| B runs, **A crashes** | game code on SH4 | C and D narrow it to update or draw; the last `[diag]` line shows the stage |
| Log has `Unhandled exception: PC …` | CPU exception | `python3 tools/symbolize_crash.py battle.elf flycast.log` → function, file:line, disassembly, caller |
| Log just stops, no exception line | hang or emulator-side crash | check Flycast's own log for `pvr_irq: ... TA_INPUT_ERR / OUTOFMEM` |
| Last line is `sprite_load(...)` / `png ok` / `vram=` | asset load/upload | note the dimensions printed; `es_sprite_64.png` is 1024×1024 (2 MiB VRAM) |

`DIAG=1` also prints struct sizes/alignments at boot, every sprite load (dimensions, VRAM address), the stage markers for frames 1–3, and a heartbeat every 300 frames.

## Step 3: send back
* the Flycast/serial log from variant A (and B if A crashes)
* the `battle.elf` that produced it (for symbolising and disassembly on this side)
* the exact `make` command and the DreamSDK/KOS version
