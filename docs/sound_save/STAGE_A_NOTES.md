# Slice 3D-7, Stage A: the audio engine - what is built, and where it departs from the plan

Plan: `docs/NEXT_SLICE_SOUND_SAVE.md` §5, §6, §9 (Stage A). A1, A2 and A3 are in the tree. The host side is tested
(`tests/sound_save`); the AICA backend (A3) has only been compiled against stub headers and **has not run on a
Dreamcast or an emulator yet**: the first build under DreamSDK and the first listen are the next step.

## Built

| Task | What | Where |
|---|---|---|
| A1 | The reference ADPCM codec and the `.adp` container; 39 procedural stand-ins (8 tracks, 31 effects) at their final lengths | `tools/audio/adpcm.py`, `tools/audio/make_standins.py`, `romdisk/audio/{music,sfx}/*.adp` |
| A1 | The same decoder in C++ and the header reader | `src/adpcm.h` |
| A2 | The cue table, its loader and its lint | `romdisk/audio/cues.txt`, `src/audio.cpp`, `tools/audio/lint_cues.py` |
| A2 | The audio logic: event names -> cues, the frame queue (once per frame, `min_gap`, 8 voices by priority, at most 8 commands), the music machine (fades on ticks, stings, ducking), options | `src/audio.h`, `src/audio.cpp` |
| A2 | The backend seam and the host's recording backend (`AC` trace lines) | `src/engine.h`, `platform/host/host_engine.cpp` |
| - | The suite: 15 checks (AU-T1, T2, T3, T4, T7, T8, the logic, the KOS stub build) | `tests/sound_save/run.sh`, `tests/sound_save/audio_unit.cpp` |

Numbers: effects 189 KiB (budget 320), music 1,827 KiB (budget 2,048), romdisk 5.08 MB (budget 5.5). The stand-ins
are in the `.elf`'s romdisk from now on, so the boot's `ram` line already includes the audio's 2.0 MiB.

Not built yet: `AUDIO_PCM8` (it needs PCM assets from the tool), A4 (`--audio-wav`, `check_audio.py`), A5 (the cue
player for the hardware listen), `make_audio.py` (real content: B4).

## A3: the AICA backend (`platform/kos/audio_kos.cpp`)

* **Effects:** each `.adp`'s data is loaded into sound RAM at boot (`snd_sfx_load_raw_buf`, 4-bit); 8 channels are
  reserved, one per voice; playing is one `snd_sfx_play_chn` command.
* **Music:** one mono ADPCM stream (`snd_stream_start_adpcm`, a 16 KiB ring = 1.5 s) owned by a thread that applies
  the game's latest request and polls every 8 ms. The game thread only writes a mailbox under a mutex.
  The callback copies full, aligned blocks from the track in the romdisk (`fs_mmap`), wrapping at the loop point.
  The stream starts queued, so its volume command lands with the start (KOS starts a channel at full volume).
* **Switches:** `make NO_AUDIO=1` (silent, as slice 3D-6) and `make NO_MUSIC=1` (effects only). `make clean` first.
* **Boot log:** `audio: AICA up, 31 effects on 8 channels, 8 tracks on 1 stream, sound RAM free N KiB`, then
  `audio: 31 effects (189 KiB), 8 tracks (1826 KiB), 7 aliases`.
* **DIAG report:** `audio: longest gap between stream polls N ms (buffer 1486 ms), poll errors N, ...`. The gap
  must stay well under the buffer's duration (DoD P2).

A first cut of Stage B1 came with it so the game is not silent after a battle: the map's track starts again after
the victory sting and after a retry (from its beginning: the owner chose option a below), and the ending's map
change keeps the dawn theme.

### What to listen for on the first run

| Moment | Expected (stand-in sounds: synthetic tones and noise, not the final audio) |
|---|---|
| Title | a slow minor phrase over a drone, fading in; it loops every ~32 s with no click |
| Menu | a click when the cursor moves, a two-tone chime on confirm |
| New game | the title theme fades out, a brighter tune fades in with the village |
| Dialog | a quiet blip as the text types |
| Battle | a faster pulse-driven track; a whoosh or a rising noise on each action and a crack when it lands (still fired when the rules resolve, so a little before the animation: Stage B2 moves them) |
| Victory / defeat | a short rising or falling phrase, then the map's track again |

If something is wrong: `make clean && make NO_MUSIC=1` tells the stream apart from the effects; `NO_AUDIO=1` proves
the rest of the game is unchanged; the boot log's `audio:` lines say what failed to load.

## What the installed KOS offers (read from its headers, git 9ba0350f)

* `snd_stream_start_adpcm(hnd, freq, st)` exists: music can stream as ADPCM (decision D1 holds, no PCM fallback needed).
* `snd_sfx_load_raw_buf(buf, len, rate, bitsize, channels)` exists and takes 4-bit ADPCM: effects can be loaded
  straight from `.adp` data (decision D3: stock `snd_sfx_*`, no upload code of our own).
* `snd_sfx_play_chn(chn, idx, vol, pan)` plays on a channel we choose: one fixed channel per voice.

## Departures from the plan

| # | Plan | Built | Why |
|---|---|---|---|
| 1 | §13.2: match the decoder state at the loop end to the loop start by re-encoding until they agree | **Reset seams**: the audio is shaped to silence over ~6 ms before the loop end and the last 24 codes are searched so the decoder lands exactly on its key-on state (0, step 127) | The chip only resets at key-on, so the first pass always starts from (0, 127); iterating gives a state that suits the later passes but not the first. With a reset seam every pass decodes to the same samples whether or not the hardware carries state across the loop. Cost: a ~6 ms dip per loop; loop points belong in a breath of the music |
| 2 | (not in the plan) | The decoder **leaks**: `x = x * 254 / 256` before each code | That is the AICA as KOS's own `adpcm2pcm` and MAME model it. KOS's `wav2adpcm` *encoder* ignores the leak; ours predicts with it |
| 3 | §5.1: the host records backend commands as `A` lines in every run | `AC` lines, in `--story` runs (where the `A` hook lines already are) | The committed goldens of the other suites stay byte-identical; `tests/barrow` reads `A` lines by position |
| 4 | §5.4: `snd_play(sfx, vol, pan)` | `snd_play(voice, sfx, vol, pan)` | The voice policy (replace the oldest of the same cue / of a lower priority) is logic; the backend must know which voice to cut |
| 5 | §5.4: `snd_music_position()` | not in the API | See the open question below |
| 6 | §7.1: 30 effects | 31: + `boss_name` | The name card's sting (`sting_boss_name`) is under 2.9 s and plays over silence: an effect, not a ninth stream track |
| 7 | §6.1: a `silent` list for events without a cue | supported, unused | Every event the code fires today has a cue or an alias |

The six `alias` lines at the end of `cues.txt` map slice 3D-6's event names (`fireball`, `hit`, `boss_skill`...) to
cues. They fire at rule resolution, as before; Stage B (B2) moves them to the 3D sequence's cues and retires them.

## Open question for Stage B1: resuming the field track after a battle

§4.6.1 wants the village or barrow track to resume **from the position it had** when the battle started. An AICA
ADPCM stream cannot start in the middle of a track: the decoder state there is not the key-on state, and a wrong
step size never corrects itself. The choices:

| Option | Cost |
|---|---|
| **a. (chosen, 2026-11-19)** The field track restarts from its beginning after every battle | none; the tracks are 28-32 s loops, the player hears their opening often |
| **b.** The tool puts a reset seam every few seconds and the track resumes at the nearest one | a ~6 ms dip at each seam (audible in sustained notes unless the composer leaves breaths there) |
| **c.** The two field tracks are 8-bit PCM (seekable anywhere) | twice their size: +~690 KiB, over the 2.0 MiB music cap unless they drop to 11 kHz |

## Stage B so far (2026-11-19, after the first listen on Flycast)

The first listen worked (music and effects play) and produced a list; this is what changed.

| Reported | Cause | Change |
|---|---|---|
| The music was bad; a low hum in the village | the placeholder generator: a drone and a random phrase | **A written soundtrack**: `tools/audio/music.py` (notes) + `tools/audio/synth.py` (instruments). The owner picked the direction from samples: a 6/8 folk dance in A dorian for the village ("D2": whistle, strings, bass, frame drum; wary rather than cheerful; nothing above B5). The other seven tracks are in the same family: `title` (the village tune, slower and fuller), `barrow` (E minor, low flute, a tolling bell), `battle` and `boss` (4/4, driven), `dawn` (the dance in A major), and the two stings. No drones |
| No sound on PRESS START | no hook there | `menu_confirm` when the title menu opens, `menu_cancel` on B (`src/screens.cpp`) |
| A lag after NEW GAME | not established; the command is issued on the press's frame | `menu_confirm` now has an instant attack (it had a soft one); to be re-checked by ear |
| No sound entering a battle | the cue existed, nothing fired it | `battle_start` on T5's first flash; the field's track is cut in 6 ticks and the battle's arrives in 12 (the fades are per cue in `cues.txt` now: `music <name> loop <file> <vol> [<fade_out> <fade_in>]`) |
| Attack sounds awful, loud, off the animation | placeholders at full level, fired when the rules resolved | The 3D battle fires its sounds from the animation sequences (`src/battle3d.cpp`): the swing 7 ticks before the clip's impact event, the hit in `impact()` with the sparks and the hit-stop, casts at the launch, impacts when the projectile lands, and the Warden's skills at their cues. `battle.cpp` only sounds for the sprite view. Combat levels are down from 220 to 120-170 |
| Sword and enemy attacks should differ | one shared pair | `swing` / `enemy_swing`, `hit_bone` (steel on bone) / `hit_hero` (a blow on the hero) / `hit_guarded`, `fire_cast` / `bolt_cast` |
| No sounds in the battle menu | no hooks | `menu_move` on every command and target change, `menu_confirm` on choosing a command and a target, `menu_cancel` on B, `menu_denied` on a locked Fire or an empty Potion (`src/ui.cpp`) |

34 effects (194 KiB) and 8 tracks (2,012 KiB of the 2,048 allowed; 187 s). `python3 tools/audio/make_standins.py
--wav DIR` writes every sound as a WAV, decoded as the console decodes it, to listen without the game.

Still to do in Stage B: footsteps, `dialog_advance`, pan by the actor's position, the music's ducking under dialog
and pause, script `sound` / `music` commands, recorded effects in place of the synthesized ones (B4).

