# Slice H2-0: "Sound, Save and Sign-off", closed

> **Status:** plan - not started. The first slice of GDD v2 (`docs/GDD_Horizontal_Slice_2.md` §16). It finishes
> slice 3D-7 (`docs/NEXT_SLICE_SOUND_SAVE.md`, called "the 3D-7 plan" below), which stays the detailed
> specification: this document says **what is left, in what order, and what GDD v2 changes about it**. Where the
> two disagree, this one wins. Every KOS call named here is checked against the installed KOS 2.3.0 (git
> 9ba0350f) before it is used; facts already read from it are marked *(read)*.

**Goal:** today's game, complete: it remembers the player (VMU save, CONTINUE, options), has the console's frame
(pause, controller removal, soft reset, boot splash), and its sound is finished (footsteps, ducking, panning, the
last hooks). Nothing in the rules, maps or story changes. After this slice every later one builds on a game that
saves.

## 1. Where slice 3D-7 stands

| 3D-7 stage | State | Left for H2-0 |
|---|---|---|
| 0 Hardware intake | tooling in; RAM known (3.25 MiB free with audio); frame times on hardware never measured | moved to H2-15 (the broadband adapter); nothing here |
| A Audio engine | codec, cue table, logic, AICA backend: built, playing in Flycast | A4 (mixdown tool); `AUDIO_PCM8` dropped (§5, D2) |
| B Sound in the game | written soundtrack (8 tracks), 34 effects, battle sounds on the animation cues, menus, the encounter | footsteps, `dialog_advance`, ducking, pan, script commands, the `A` timeline golden; the owner's notes from the second listen |
| C Save, continue, options | nothing | all of it |
| D Pause, controller, splash, reset | nothing | all of it |
| E Sign-off | nothing | docs and licences here; the hardware pass and the playtest move to H2-15 |

## 2. What GDD v2 changes

| # | 3D-7 plan | H2-0 | Why |
|---|---|---|---|
| V1 | a fixed 128-byte payload, version 1 | the same fields behind a **header that carries version and size**, read by a loader that rejects what it does not know; the payload's reserved area is sized for H2-7's party, inventory and third slot (payload <= 256 bytes) | H2-7 extends the save; v1 saves must be told apart cleanly, not misread |
| V2 | saved at every checkpoint | unchanged **for this slice** (the game has no save points yet); the request goes through one function so H2-7 can move it to save points | GDD v2 10.4 |
| V3 | pause menu on Start | unchanged; **X is left free** for H2-7's party menu | GDD v2 3 |
| V4 | the log is forced to the serial port | the log goes to dcload's console when dcload-ip is present (`dcload_type == DCLOAD_TYPE_IP`, device name `"fs_dcload"` *(read)*), else serial | the owner's hardware runs over a broadband adapter: a forced serial log shows nothing there |
| V5 | "the first VMU found" | the first memory card, scanning A1, A2, B1..., that **already holds our save**, else the first with enough free blocks | the owner's setup has two cards on port A |
| V6 | hardware frame-time sign-off, new-player playtest (3D-7 DoD P1-P4, G1-G2) | moved to **H2-15** | they need the console and the finished chapter |

## 3. Work, in order

Sizes: S about an hour of agent work, M two or three. Each step ends with the suites green.

### Step 1 - Sound, finished (M)

| Task | Content | Check |
|---|---|---|
| 1a | The owner's notes from the second listen (new soundtrack, combat sounds, the NEW GAME lag): fix what they name | by ear, the owner |
| 1b | **Footsteps**: `step_grass` / `step_stone` on the walk clip's two foot contacts, by the tile under the hero (path tiles = stone) | `AC` lines only while walking, never two in 8 ticks |
| 1c | **`dialog_advance`** on A at a full page; no blip during a skip | route trace |
| 1d | **Ducking**: 60 % under a dialog box (12 ticks down, 12 up), 50 % in pause, 40 % under the name card's sting | unit + route |
| 1e | **Pan**: battle cues take the actor's screen x (-0.6..0.6 of full); MONO centres | unit |
| 1f | **Script commands** `sound <cue>` and `music <cue>|stop [ticks]` in `src/script.cpp` **and** `tools/dialog/lint.py`; the ending's cheer and the boss intro's rise use them | the dump-parity test |
| 1g | **Mixdown** (3D-7 A4): `--audio-wav FILE` renders a route's `AC` lines with `src/adpcm.h`; `tools/audio/check_audio.py` reports peaks, long silences and levels | AU-T5 |
| 1h | The full route's `AC` timeline as a committed golden (AU-T6) | suite |

### Step 2 - Platform odds and ends (S)

| Task | Content |
|---|---|
| 2a | Log device (V4). The DIAG and boot lines keep their own flush for serial; dcload needs none |
| 2b | `eng::pad_present()` (port A has a controller) and the **X button** in `eng::Key` (unused until H2-7) |
| 2c | `eng::sys_mono()` from the BIOS setting (`flashrom_get_syscfg`; field name to be read in `dc/flashrom.h`) |
| 2d | Soft reset: this KOS registers **no** default A+B+X+Y+Start handler *(read: no `cont_btn_callback` call in the kernel)*, so the combination is simply read by the game |

### Step 3 - Save, pure logic (M)

`src/save.*`, `src/options.*`, the host's VMU emulation (`--vmu DIR`, `--vmu-fault absent|full|fail-write|corrupt|pull-mid-write`).

* Payload: magic `HYM1`, **version, size**, save counter, the checkpoint's `Progress` (32 bytes, as it is today),
  `has_checkpoint`, cleared, best rank, best time, `SessionStats`, options, reserved to 252, CRC32.
* The VMU package (header, icon, descriptions) is built by our code into a static buffer (3D-7 §5.3):
  `tools/make_vmu_icon.py` makes the 32x32 icon from the logo's crest.
* Loading checks, in order: file, package, magic, version, size, CRC, then `Progress` sanity (stage, map, a
  walkable checkpoint tile). Any failure greys CONTINUE with its reason.
* Tests: SV-T1 (round trip, block count), SV-T3 (damaged saves, sanitizers on), SV-T4 (faults), SV-T6 (heap).

### Step 4 - Save on the Dreamcast (M)

`platform/kos/vmu_kos.cpp`: a worker thread that scans (V5), reads at boot, and writes on request
(`vmufs_read` / `vmufs_write` with `VMUFS_OVERWRITE`). The game thread copies a snapshot and returns. `make NO_VMU=1`
compiles it out (CONTINUE greyed, "Saving disabled").

*Not testable here:* this is compiled against stub headers only. First real run: Flycast, which emulates both of
the owner's memory cards.

### Step 5 - CONTINUE, the title menu, messages (M)

* Title: CONTINUE (greyed with a reason line) / NEW GAME / OPTIONS / CREDITS; the cursor starts on CONTINUE when
  it is lit. The box grows from 88 to 132 px.
* Save requests at C0-C3, Brom's rest and the results card; the "Saving..." indicator (at least 45 ticks); the
  once-per-power-on messages of 3D-7 §4.5.
* CONTINUE restores **exactly a death retry's state** at the saved checkpoint (SV-T2, at every checkpoint on both
  maps); the crest and the best result survive a power cycle (SV-T5).

### Step 6 - Options (S)

Music 0-10, effects 0-10, stereo / mono (default from the BIOS). A screen from the title, an overlay from pause;
saved with the next save. A **sound test** row (plays any cue; could) doubles as the 3D-7 plan's A5 cue player.

### Step 7 - Pause, controller, reset, splash (M)

* **Start in the field**: PAUSED - Resume / Options / Return to title (confirm, NO preselected). No game tick while
  paused; the results clock stops; pausing changes no trace line but its own (PA-T1).
* **Controller removed**: pause in the field; a "Please reconnect the controller" overlay elsewhere; a fresh press
  afterwards. Not in the attract demo.
* **A+B+X+Y+Start**: to the title from any mode, never out to the loader.
* **Boot splash**: 20 + 80 + 20 ticks, skippable after 30; the audio boot and the VMU read run under it.
* Tests: PA-T1..3, FL-T1..3 (the new screens swept for the vertex budget).

### Step 8 - Docs and close (S)

`docs/SLICE_SOUND_SAVE.md` (built, measured, deviations), HOWTO (a cue, a track, a save field, an option), TRIAGE
(VMU), `docs/LICENSES.md` (code, models, textures, fonts, audio - the audio is all generated in this repository),
`docs/sound_save/HARDWARE_CHECKLIST.md` for the Flycast pass now and the console pass in H2-15.

## 4. What the owner does

| When | Action |
|---|---|
| Before step 1 | play the current build and send notes on the new soundtrack and effects (per track: keep, adjust, redo) |
| After steps 1, 4-5, 7 | `make clean && make DIAG=1`, play in Flycast, send the log and what was seen and heard |
| After step 5 | the save checks: save, close Flycast, reopen, CONTINUE; remove the memory cards in Flycast's settings and boot; fill one card |
| Any time | commit (changes are left uncommitted in the working tree, as agreed) |

## 5. Decisions

| # | Question | Recommendation |
|---|---|---|
| D1 | Save file size now | 2 blocks (3D-7); it grows to 3 in H2-7 with the party |
| D2 | `AUDIO_PCM8` kill switch | drop it: ADPCM plays correctly in Flycast, and the switch needs a second set of assets |
| D3 | NEW GAME over an existing save | no confirmation (3D-7 D6): the save is replaced at the first checkpoint |
| D4 | After the results card | the save keeps the crest, best result and options; CONTINUE greyed "Cleared - start a NEW GAME" (3D-7 D7) |
| D5 | Sound test in OPTIONS | yes, if step 6 has room; it costs one row and removes the need for a special build |

## 6. Definition of done

* All suites green, `tests/sound_save` extended with the 3D-7 plan's SV-, PA- and FL- tests and AU-T5 / AU-T6.
* `make clean && make` under DreamSDK with 0 warnings, also with `NO_AUDIO=1`, `NO_MUSIC=1`, `NO_VMU=1`, `DIAG=1`.
* In Flycast: the game saves at every checkpoint without a hitch, CONTINUE works after a restart, every VMU fault
  shows its message and the game goes on; pause, reconnect and soft reset behave as specified; the splash hides
  the load; every moment of the 3D-7 plan's music map and cue sheet is heard.
* No heap allocation on the game thread after boot; the audio and save budgets of the 3D-7 plan §8 hold.
* `docs/SLICE_SOUND_SAVE.md` records what was built and what moved to H2-15.

## 7. Risks

| Risk | Mitigation |
|---|---|
| The VMU worker and the save path cannot be run on the host | all logic is in `src/save.*` and tested there; the KOS file is thin and compiled against stubs copied from the real headers; first run in Flycast with DIAG lines at every step |
| VMU writes disturb controller polling | the worker thread; if input stutters in Flycast or on hardware, save only under a fade (3D-7's fallback) |
| Flycast's VMU emulation is more forgiving than a real card | the fault cases are proven on the host; the real-card pass is in H2-15 |
| Pausing changes game timing | the pause never calls `game_tick`; PA-T1 compares traces with and without pauses |
| The second listen asks for more audio work than step 1a allows | audio content is data (`tools/audio/music.py`, `cues.txt`): it can continue beside steps 3-7 |
