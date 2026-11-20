# Slice 3D-7, Stage 0: hardware intake

Plan: `docs/NEXT_SLICE_SOUND_SAVE.md` §3. Stage 0 measures slice 3D-6 on a Dreamcast before any audio or save
code lands. Tasks 0A-0C and A0 are in the tree; 0D (the playthrough) needs the console.

## What changed

| Task | Change | Where |
|---|---|---|
| 0A | The 300-frame DIAG report names the current scene; a `scene ...` line sums up each scene when it ends | `platform/kos/main.cpp` (`scene_probe`), `src/game.cpp` (`game_scene_tag`) |
| 0B | Two `ram ...` lines in every build's boot log (before and after `game_init`); the host prints `D ram:` with `--resources` | `platform/kos/main.cpp` (`ram_report`), `platform/host/trace_main.cpp` |
| 0C | DIAG builds count the PVR's six error events and print them with each report | `platform/kos/main.cpp` (`ta_err_*`) |
| A0 | `romdisk.img` depends on every file under `romdisk/` | `makefile` |

The KOS calls were read from the installed KOS (2.3.0, git 9ba0350f): `dc/asic.h`, `kos/linker.h`,
`arch/arch.h`, `arch/stack.h`, `kernel/mm/mm.c`, `hardware/pvr/pvr_init_shutdown.c`. This KOS registers the PVR
error handlers only when built with `PVR_RENDER_DBG`, so the DIAG handler normally has nothing to chain to.

## The run (0D)

1. `make clean && make DIAG=1` under DreamSDK. Report any warning.
2. Capture the serial log from power-on.
3. Play: title, wait 40 s for the attract demo, NEW GAME, the village, at least one patrol battle, the barrow,
   lose to the Warden once, retry, win, the ending, credits, results, back to the title. Leave the title up for
   5 s at the end so the last scene's line is printed when it changes (or start a new game).
4. Send the whole log and the filled-in `docs/barrow/HARDWARE_CHECKLIST.md`.

## Reading the log

```
[boot  412 ms] ram after game_init: image end 8c7a1234 (image 7748 KiB, of it BSS 2900 KiB), heap 640 KiB claimed / 512 KiB in use, free main RAM 7900 KiB (7772 never claimed + 128 free in the heap) of 16384 KiB
[diag] scene field:village: 5400 frames, worst CPU frame 9100 us, average 6200 us, frames over 16.7 ms: 0, most ticks in a frame 1, TA errors 0
[diag] frames 6000 [battle:patrol]: worst CPU frame 8700 us, frames over 16.7 ms: 0, heap in use 524288 B (+0 since last report)
[diag]   TA errors since boot: isp_oom 0, strip_halt 0, param_oom 0, opb_oom 0, input_err 0, input_overflow 0
```
(The numbers above are placeholders that show the format, not measurements.)

* **Scenes:** `title`, `card`, `attract:<map>`, `field:<map>`, `dialog:<map>`, `to_battle:patrol|boss`,
  `battle:patrol|boss`, `to_field:<map>`, `map_change:<map>`, `death:<map>`, `credits`, `results`, `to_title`.
  Cutscenes run in `dialog:<map>`. Load hitches under a fade show up in `map_change`, `to_title` and the first
  frames after boot, apart from the gameplay scenes.
* **"over 16.7 ms"** counts CPU work (ticks + building the scene), not the wait for the PVR.
* **free main RAM** is what §8 needs: the audio adds about 2.4 MiB to the image and must leave 1.5 MiB.
* **TA errors** must stay 0 (`param_oom` = the 1 MiB TA vertex buffer overflowed).

## Exit

Every scene has a worst frame under 16.7 ms, free RAM is known, TA errors are 0, and §8's TBD cells in the plan are
filled in. A scene over budget is fixed before Stage A.
