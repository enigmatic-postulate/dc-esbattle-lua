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
* 32 px tiles: a 640×480 screen shows 20×15. Maps larger than that scroll, and the camera clamps at the edges.
* Movement is free, in 8 directions (2 px/tick). The hero collides with a 16×12 "feet" box (`HERO_BOX_*` in `src/field.h`), so 1-tile corridors are easy to walk, and he slides along walls. Entities block with a 24×20 body box; touching one starts its encounter.
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

Input scripts are one line per 60 Hz tick: `<dt> <buttons-hex>` (dt is ignored). Bits: up 1, down 2, left 4, right 8, A 16, B 32, Y 64, Start 128; combine bits for diagonals (up+right = 09). Walking straight covers one tile per 16 ticks.

**Battle controls.** Command ring: ◀▶ choose, A select, Y jumps to Guard. Attack → pick a target (◀▶, A), B back. Fire → confirm step with every enemy marked (A cast), B back. Guard casts immediately.

## Preview without hardware

```sh
./build/battle_trace_f64 tests/inputs/field_full.in /tmp/full.trace        # full trace (with draw calls)
python3 tools/render_trace.py /tmp/full.trace 60 romdisk /tmp/frame60.png   # approximate PVR render of tick 60
```
