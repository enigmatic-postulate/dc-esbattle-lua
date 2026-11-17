# tests/dialog: the dialog slice's checks (slice 3D-5)

Run everything with `tests/dialog/run.sh` (`QUICK=1` for a shorter pass; `BPY=` and `SH4=1` add the Blender rebuild
and the SH4 measurement). What each check proves: `docs/SLICE_DIALOG.md` §7.

| File | What it is |
|---|---|
| `run.sh` | the suite |
| `routes/*.route` | story routes for the story driver (`platform/host/story.cpp`): the order variations of the plan (§4: `r1`-`r6`) and the checkpoint routes (`cp_s0`, `cp_s3`); each lists its expectations (`expect key=value`) |
| `ui.route`, `fixtures/ui_village.txt` | scripted inputs and a test script that exercise every dialog UI rule (typewriter pauses, B speed, A, choices with and without cancel, a wait, a fade with a sys page, Rowan's slot, a variant change) |
| `check_dialog.py` | the dialog UI rules, tick by tick, against an independent model (`G` / `I` / `N` trace lines + the compiled dump) |
| `check_talk.py`, `fixtures/talk_map.txt` | the talk prompt's geometry (1.3 tiles, 60 degrees, nearest, ties) against a model; the test map puts two NPCs side by side |
| `check_checkpoints.py` | retries restore the checkpoint byte for byte, the hero's tile, full HP, the defeated entities |
| `check_firelock.py` | the learned-Fireball rule on the 3D battle's UI |
| `check_widths.py` | text metrics: C++ == `tools/dialog/lint.py` |
| `check_lint_fixtures.sh`, `lint_bad/*.txt` | 23 broken scripts: the lint refuses each with its message, the game's compiler the ones it checks |
| `sh4_model.py` | the SH4 instruction estimate from host counters (calibrated on slice 3D-4's qemu measurement) |
| `sh4_budget.sh` | the real SH4 measurement under qemu (`SH4=1`; tests/sh4zam/README.md) |
| `kos_stub/` | stub KallistiOS headers: a host syntax/type check of `platform/kos/main.cpp` (nothing is linked or run) |

Story driver steps: `talk <npc>`, `chase <enemy>`, `goto <x> <y>`, `choose <k|cancel> ...`, `policy <bot policy>`,
`wait <ticks>`, `press <button>`, `hold <button> <ticks>`, `stick <x> <y> <ticks>`, `open <npc>`, `manual`,
`pace <ticks>`, `expect <key>=<value> ...` (the header of `platform/host/story.cpp`).
