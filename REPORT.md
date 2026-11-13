# Lua → C++ port: script assessment and porting method

**Scope.** This covers every `.lua` file in `zip/romdisk/`, how they interact with `zip/main.cpp`, and a recommended way to move the battle engine fully into C++. I used the method end to end so it could be judged on results, not on a plan. A complete C++ port and its verification harness are in this directory (`port/`). The original project (`zip/`) is **unchanged**.

---

## 1. Summary

* **What exists:** 17 Lua files with 1,717 lines, plus a 610-line C++ host. **15 files are live and 2 are dead**: `main.lua` is an old ship-and-bullets demo, and `battle/input_util.lua` is never loaded. The Lua VM is **Lua 5.5.0**, built from DreamSDK's kos-ports (identified from strings in `sprite_demo.elf`).
* **Recommended method:** a manual, one-C++-file-per-Lua-module port, done **in one step** (not incrementally). Every line is checked against the *original, unmodified* Lua scripts by **differential trace testing**: the same inputs go into both, and every draw call plus the logic state is compared each frame. I recommend against automatic translation tools (§4).
* **Result:** the full port was done (1,343 lines of C++, no Lua). It matched the original Lua on **138 of 138 test runs: 403,338 frames and 7.2 million draw calls compared**, covering scripted scenarios, three frame-timing modes and 120 fuzz runs. It compiles cleanly with `-Wall -Wextra`, in C++11 and pedantic mode.
* **The harness catches real porting mistakes.** I injected 20 realistic porting errors one at a time, and it **caught 16**. Each of the 4 it missed **cannot be observed with current content**. One survives only *because* Guard never works, which independently confirms bug #1 (§6.3).
* **The port keeps every bug for now.** I found **several real gameplay bugs in the Lua** and confirmed each against the original scripts. For example, **Guard never reduces damage**, **B-cancel and the Y shortcut do nothing**, and **holding A across a turn change inverts the A button**. The port reproduces them on purpose, so "matches the Lua" stays a hard, testable claim. They should be fixed *after* the switch, as separate changes (§5).
* **Performance, honestly:** today's Lua cost is real but small. The C++ port removes the garbage-collector load (~200–230 bytes of garbage per combat frame), the ~80 KiB Lua VM code, the ~80 KiB Lua heap and 3 of the 16 sprite slots. On the host the port runs the battle logic about 50–100× faster. The bigger wins are **predictable frame times, fixed memory, and compile-time type checking** (§7).

---

## 2. Inventory and assessment of the Lua scripts

### 2.1 Load graph (what actually runs)

```
main.cpp ──luaL_dofile──> boot.lua ──> game/init.lua ──> battle/battle.lua
                                                            ├─ battle/turn_system.lua ─> status.lua
                                                            ├─ battle/targeting.lua
                                                            ├─ battle/presentation.lua   (sprite.load plasma #1)
                                                            ├─ battle/fx.lua
                                                            ├─ battle/status.lua ─> combat.lua, fx.lua
                                                            ├─ battle/ui.lua ─> targeting, data/skills, input_probe
                                                            │     input_probe.lua          (sprite.load plasma #2)
                                                            │     ui.lua cursor            (sprite.load plasma #3)
                                                            ├─ battle/hpbar.lua ─> assets.lua (plasma #4, cached)
                                                            ├─ data/skills.lua ─> combat, status, fx
                                                            └─ data/encounters/test_slimes.lua ─> assets.lua
main.cpp per frame:  update(dt) -> draw() -> flush sprite batches / rect queues
```

### 2.2 Per-file assessment

| File | Lines | Status | Role | Porting notes | C++ file |
|---|---:|---|---|---|---|
| `boot.lua` | 10 | live | defines global `update`/`draw` | becomes two direct calls in `main()` | `platform/kos/main.cpp` |
| `game/init.lua` | 14 | live | pass-through | removed | – |
| `assets.lua` | 19 | live | path → sprite cache | 3 modules **skipped the cache** (4 uploads of `plasma.png`) | `assets.cpp` |
| `battle/battle.lua` | 226 | live | state machine: command → resolve → animate → turn_advance; win/lose; draw order | one state per frame (cadence is visible on screen); a stray `sprite.draw` runs at module load | `battle.cpp` |
| `battle/turn_system.lua` | 51 | live | agility order, skip handling | **`table.sort` is unstable** (§3.2) | `turn_system.cpp` |
| `battle/combat.lua` | 54 | live | damage math | duplicates `status.has` to avoid a circular `require`; `add_status` unused | `combat.cpp` |
| `battle/status.lua` | 157 | live | burn/guard/stun, hook table | hook closures → function-pointer table; `get`/`remove` unused | `status.cpp` |
| `battle/targeting.lua` | 77 | live | target selection by mode | actor *references* → `Actor*` | `targeting.cpp` |
| `battle/presentation.lua` | 98 | live | lunge/recoil offsets, flashes | swap-remove order is visible | `presentation.cpp` |
| `battle/fx.lua` | 172 | live | floating numbers drawn as 7-segment rectangles | uses `math.random`; **letters draw as dots** | `fx.cpp` |
| `battle/hpbar.lua` | 97 | live | HP bar + status pips | straightforward | `hpbar.cpp` |
| `battle/ui.lua` | 251 | live | command ring, targeting, clicks | the most delicate module (§3.3, §5) | `ui.cpp` |
| `battle/input_probe.lua` | 69 | live | button reads + debug strip | `pressed`/`reset_edges` have no effect | `input_probe.cpp` |
| `data/skills.lua` | 167 | live | attack/fireball/guard/items | closures → function-pointer table; AOE uses float math | `skills.cpp` |
| `data/encounters/test_slimes.lua` | 49 | live | encounter data | missing stats are made explicit | `encounters.cpp` |
| `main.lua` | 107 | **dead** | old ship demo | not ported | – |
| `battle/input_util.lua` | 99 | **dead** | input alias experiment | not ported | – |

**Engine calls from Lua** (defined by `register_api` in `main.cpp`): `sprite.load/draw`, `gfx.rect/rect_tr`, `input.down/axis`, `dbg.print/set_pose`. `input.axis` and `dbg.set_pose` are never called. `dbg.print` stores text in a buffer that nothing ever displays. `register_api` sets the `gfx` global twice, and `package.path` is set twice, the second time *after* `boot.lua` has already run.

### 2.3 Overall quality of the Lua code

The scripts are small and well organised (update → logic → draw, with clear module boundaries), which makes them a good candidate for porting. Debt comes from the prototype stage:
* many defensive `x or y or z` fallbacks for fields that always exist
* polarity and binding guesswork in the input code, including an entire dead module for it
* rule changes made inside presentation hooks
* state changes inside `draw()`

None of this blocks a port. Several of these items *are* the bugs listed in §5.

---

## 3. Porting hazards found (things a naive port gets wrong)

Each hazard was either found by the harness or confirmed with it. Each is handled explicitly in the port.

### 3.1 Number semantics (Lua 5.5 integers/floats vs. C++)
* `math.floor` returns an *integer* in Lua 5.3 and later. HP and damage stay integers throughout, so the port uses `int` for all rule math.
* **Fireball damage `floor(base * 0.65)` must not be ported as float arithmetic.** In float32, `20 * 0.65f` = 12.9999995, which floors to **12** instead of 13. The port uses `base * 65 / 100`, which is exact for every non-negative `base`. Rule: **game rules use integer math.**
* Timers (`anim_t`, durations, the target-entry lock) accumulate `dt`. In `float` vs `double`, a threshold like `t >= 0.20` can fire one frame apart (§6.2). The port uses a `real` type alias: `float` on hardware, and `double` in the harness build to tell logic errors apart from precision effects.

### 3.2 `table.sort` is unstable
The test encounter has three slimes with equal agility. The shipped build's turn order is **hero, slime_b, slime_a, slime_c**, not the order they're listed in. `std::sort` or `std::stable_sort` would each produce a *different* order. The port deliberately uses a stable insertion sort, so **ties keep encounter order**. This is the *only* intentional behaviour change in the game logic. The harness runs the Lua reference with a stable `table.sort` shim, and `--native-sort` shows the divergence at frame 1.

### 3.3 Side effects inside conditions
`ui.lua`'s click detection changes latch state *inside* conditions, so **evaluation order is behaviour**:
* `if B_click() and ui_state == "target"` must call `B_click()` every frame. Swapping the operands is a real bug, and the harness catches it.
* `if target_entry_lock == 0 and A_click()` must *not* sample A during the lock. Swapping is a bug that appears only when A is pressed exactly on the frame the lock expires. A dedicated sweep test (`lock_edge`) was added to catch it.
* `if L_click() ... elseif R_click()`: converting this to two independent `if`s changes behaviour when L and R are pressed together. Chord fuzzing was added to catch it.

### 3.4 Order-observable containers
`t[i] = t[#t]; t[#t] = nil` (swap-remove) decides draw order for floating text and flashes. The port's `FixedVec::swap_remove` reproduces it exactly. Replacing it with `erase()` would quietly reorder sprites that overlap.

### 3.5 Frame cadence
`battle.lua` advances **at most one state per frame**: command → resolve → animate → turn_advance → command each take a frame. The C++ `switch` keeps this. Merging states "to save a frame" changes animation timing, and the harness catches it.

### 3.6 References vs. copies
Targets are references to the actor tables in `ctx.actors`. The Lua even has a `canon_target` helper in case of copies. The port uses `Actor*` into a fixed array that never reallocates, and `BattleCtx` is non-copyable, so copies can't happen.

### 3.7 Randomness
`fx.lua` uses `math.random()` for the sideways drift of floating numbers. The engine seam exposes `eng::random01()`. The harness feeds both sides the same 24-bit random generator, which is exactly representable in float and double.

---

## 4. Method evaluation

| Option | Removes Lua VM? | Idiomatic C++? | Verifiability | Verdict |
|---|---|---|---|---|
| **A. Manual 1:1 module port + differential trace harness** | yes | yes | **strong** (checked every frame against the real scripts) | **Recommended**, and demonstrated |
| B. Auto-translation (e.g. lua2c-style → C API, Pallene/Titan-style typed subsets) | **no**: the output still calls into the Lua runtime | no | weak | Rejected: defeats both goals |
| C. Precompile to bytecode (`luac`) | no | n/a | n/a | Saves load time only; not a port |
| D. Incremental "strangler" (port one module, bind it back into Lua) | eventually | yes | medium | Rejected *at this size*: actor tables are shared by reference across every module, so each intermediate step would need throw-away marshalling code. Worth revisiting only if the scripts grow by 10× before porting |
| E. Rewrite from scratch / port without a harness | yes | yes | **none** | Rejected: the hazards in §3 are exactly what slips through review |

### 4.1 The recommended procedure (what was done here)

1. **Capture a reference.** Run the unmodified romdisk scripts headless under the *same VM version* (Lua 5.5, via `lupa`). Mock the engine calls so that each one is logged per frame, along with a snapshot of battle state (`harness/lua_ref.py`).
2. **Define the seam.** Replace the Lua-facing engine calls with a small C++ interface (`src/engine.h`), with one backend for the Dreamcast and one for host tracing.
3. **Port one module per file**, using the translation rules in §4.2. Keep behaviour **bug-for-bug**, marking legacy behaviour with `LEGACY:` comments.
4. **Prove equivalence.** Run scripted scenarios × frame-timing modes × fuzz seeds through both and compare traces (`harness/run_suite.sh`).
5. **Test the tests.** Inject likely porting mistakes and check the suite catches them (`harness/mutate.py`). Each surviving mutant either needs a targeted test (this happened twice here) or is explained as unobservable with current content.
6. **Switch over.** Drop `-llua` and delete the `.lua` files from the romdisk.
7. **Then fix bugs**, one reviewed change each. Every fix intentionally changes the trace, and the harness shows exactly what changed.

### 4.2 Translation rules

| Lua idiom | C++ form |
|---|---|
| actor / record tables | `struct` with explicit fields; `x or default` fallbacks move into the data (`encounters.cpp`) |
| string ids (`"burn"`, `"enemy_all"`, `"command"`) | `enum class` |
| tables of closures (`resolve`, `present`, `on_turn_start`) | function-pointer tables; keeps the data-driven shape |
| module-local state (`moves`, `floats`, `ui_state`) | fields of `BattleCtx` (resettable, testable) |
| `t[#t+1] = v` growth | `FixedVec<T,N>`: capacity stated up front, no heap use per frame |
| 1-based indices and wraparound | 0-based, `% n` wrap; the harness checks every cursor path |
| `a and f()` / `elseif` with side effects | keep the evaluation order exactly |
| `math.floor(x * 0.65)` | integer arithmetic |
| `table.sort` | explicit, stable, tie-break documented |
| `math.random` | `eng::random01()` (injectable) |

---

## 5. Bugs and quirks in the current Lua (kept in the port; fix afterwards)

Each item was confirmed by running the **original scripts** in the harness. Each has a `LEGACY:` marker in the port.

| # | Severity | Finding | Evidence | Suggested fix |
|---|---|---|---|---|
| 1 | **High** | **Guard does nothing.** `guard` is added with `turns=1` during the hero's turn. `turn_advance` then ticks the *hero's own* statuses, so Guard expires before any enemy acts. | trace: `hero:20[guard:1]` → next frame `hero:20[]` → slime hits for 2 (Guard would make it 1) | expire Guard at the start of the owner's next turn, or apply it with `turns=2` |
| 2 | **High** | **B (cancel) and Y (Guard shortcut) do nothing.** `l_input_down` maps only d-pad and `"fire"`, so `"b"`/`"y"`/`"start"` are always false. Once in attack targeting, the player cannot back out. | with the shipped binding, B leaves the UI in TARGET mode; with B/Y mapped it returns HOME | map `CONT_B/CONT_Y/CONT_START`. **The port's KOS input layer does this** (the one deliberate platform change) |
| 3 | **High** | **A becomes inverted after being held across a turn change.** `ui.lua` treats "pressed" as "different from the value sampled at `reset()`". If A is held during `reset()`, *releasing* A counts as a click, and actions fire on release until the next turn. | holding A from frame 7 through the enemy turns, then releasing at frame 157, entered attack targeting with no press | normal edge detection (`now && !prev`); the C++ input layer already reports correct polarity |
| 4 | Medium | Turn order with equal agility depends on the unstable `table.sort` | §3.2 | make the tie-break explicit (the port does: encounter order) |
| 5 | Medium | Status text (`BURN`, `GUARD`, `SKIP`) draws as rows of dots; the 7-segment font has only digits, `-` and `:` | `fx.lua` fallback branch | add a small bitmap font |
| 6 | Low | Guard shows "GUARD" twice (once from `on_apply`, once from `present`) | `status.lua` + `skills.lua` | remove one |
| 7 | Low | Burn is applied in Fireball's *presentation* hook, not its rules step | `skills.lua` | move it to `resolve`; presentation shouldn't change rules |
| 8 | Low | `recoil` always pushes left, so enemies recoil *toward* the hero | `presentation.lua` | push away from the attacker |
| 9 | Low | Enemy wobble uses `anim_t`, which only advances during `animate`, so enemies freeze mid-tilt between actions | `battle.lua` draw | use a free-running timer |
| 10 | Low | `win`/`lose` are terminal with no handling or screen | `battle.lua` | add an outcome flow |
| 11 | Low | `rebuild_order` ignores `start_turn`'s result, so a stunned first actor would still act | `battle.lua` | honour `can_act` |
| 12 | Low | `ui.draw` changes `target_i` (clamps it); a stray `sprite.draw` runs at module load | `ui.lua`, `battle.lua` | move the clamp to update; delete the stray draw |
| 13 | Low | The same `plasma.png` is uploaded 4× (4 of 16 slots); `dbg.print` output is never shown | `presentation/ui/input_probe.lua`, `main.cpp` | the port uses one cached texture and logs via `dbglog` |

Adjacent finding, outside Lua: `es_sprite_64.png` is **1024×1024**, so it takes 2 MiB of the 8 MiB VRAM to draw at 64×64. Downscale the source to 64×64.

---

## 6. Verification results

### 6.1 Equivalence (C++ `real = double` vs. original Lua 5.5)

| Suite | Runs | Frames | Engine calls compared | Result |
|---|---:|---:|---:|---|
| Scripted scenarios: fireball win, attack win (retargeting), guard → lose, cancel/Y, hold-A, lock-edge sweep; × 3 timing modes | 18 | 43,218 | 684,717 | **all equivalent** |
| Fuzz (single buttons, holds, chords); 40 seeds × 3 timing modes | 120 | 360,120 | 6,539,606 | **all equivalent** |
| **Total** | **138** | **403,338** | **7,224,323** | **138/138** |

Largest coordinate difference: 3.1e-5 px, caused only by the C side casting to `float`, which `main.cpp` already does. Alpha: exact. State snapshots (state, active actor, turn, order, HP, statuses): exact.

### 6.2 Hardware precision (C++ `real = float`)
**131/138** runs are equivalent within 0.01 px and ±1 alpha. **All 7 divergences are in the `jitter` timing mode** (irregular 1–120 ms frames). In each, a timer crossed its threshold one frame apart: a hurt marker lingering a frame, or a floating number expiring a frame earlier. The same source passes 138/138 when built with `double`, so this is precision, not porting logic. At a steady 60 Hz (`ms60`) and in `dyadic` mode, float32 matched on every run.

Which precision the *shipped* Lua effectively uses depends on the toolchain's SH4 floating-point setting. With single-precision-only builds, Lua's `double` is itself 32-bit, and the original has the same jitter. **Recommendation:** switch gameplay timers to integer ticks (frames or microseconds). That makes timing deterministic at any precision, and makes input replays reliable.

### 6.3 Harness sensitivity (mutation testing)
20 realistic porting mistakes were injected one at a time (`harness/mutate.py`); full results are in §6.4. My first version of the suite missed two of them:
* the `and` operand swap on the A button
* `elseif` → independent `if`

Those gaps led to two additions: the **lock-edge sweep** and **chord fuzzing**. Both mutants are now caught. Mutants that survive do so because **current content never exercises that code**. For example, the minimum-1 damage clamp never activates, because `str − def ≥ 2` for every pairing. Takeaway: **the harness can only verify what the content exercises.** Add a few test-only encounters (high-defence enemy, stun skill, 4+ statuses) as content grows.

Coverage (gcov over the whole suite): every line of the port that current content can reach is executed. The unexecuted lines are unreachable in *both* implementations: stun, pip overflow, the `-`/`:` glyphs, the Items skill, `Self`/`AllyAll` selection, `status_get/remove`, and asset-load failure. These are checked only by code review for now.

### 6.4 Mutation results (score 16/20; raw log: `results/mutation.log`)

| Injected porting mistake | Result | First caught by |
|---|---|---|
| recoil direction flipped | caught | attack_win, frame 29 |
| swap-remove loop skips the swapped-in element | caught | attack_win, frame 73 |
| status expires at `< 0` instead of `<= 0` | caught | cancel, frame 192 |
| home-ring left wrap wrong | caught | cancel, frame 45 |
| target-entry lock dropped | caught | lock_edge, frame 11 |
| Lua `and` operand order swapped (B cancel) | caught | lock_edge |
| Lua `and` operand order swapped (A confirm) | caught | lock_edge *(missed before that test existed)* |
| `elseif` ported as independent `if` | caught | chord fuzz *(missed before chord fuzzing existed)* |
| Burn applied in resolve instead of present | caught | cancel, frame 52 |
| alpha rounding `+0.5` dropped | caught | attack_win, frame 30 |
| hurt marker only drawn for living actors | caught | attack_win, frame 368 |
| UI idle-baseline replaced by edge detection (i.e. *fixing* bug #3) | caught | hold_a |
| state transition and animation in the same frame | caught | attack_win, frame 28 |
| pip fill drawn before border | caught | cancel, frame 52 |
| enemy wobble amplitude 0.10 → 0.11 | caught | attack_win, frame 29 |
| turn index wrap wrong | caught | cancel, frame 192 |
| `erase()` instead of swap-remove for flashes | missed | unobservable: flashes created together expire together |
| min-1 damage clamp forgotten | missed | unobservable: `str − def ≥ 2` for every current pairing |
| AOE `×0.65` replaced by `×2/3` | missed | unobservable: base damage is always 5, and both give 3 |
| Guard rounding `(d+1)/2` → `d/2` | missed | unobservable **because Guard never takes effect** (bug #1) |

### 6.5 Build status
* Host: `make host` builds with `g++ -std=c++11 -Wall -Wextra` with **no warnings**; also clean with `-pedantic -Wshadow`.
* Dreamcast: `platform/kos/main.cpp` + `makefile` are **not compiled for SH4** (no KOS toolchain here). The file passes a syntax/type check against a minimal stub of the KOS API. Every KOS call is copied from the original `main.cpp`, which did build. **First step on your side:** run `make` under DreamSDK and compare on Flycast/hardware.

---

## 7. Performance assessment

| Metric | Lua build | C++ port | Notes |
|---|---|---|---|
| Garbage per combat frame | **~200–230 bytes** (~13 KB/s at 60 fps) | **0**; no heap use per frame | measured with GC stopped; depends on content, not platform |
| Battle-logic CPU per frame (host x86) | 16–22 µs | 0.16–0.41 µs | ~50–100×; **indicative only**, not Dreamcast timings |
| Lua VM code in ELF | ~80 KiB (27 Lua `.c` files, found in the ELF's symbol table) | 0 | plus `-llua` link time |
| Lua heap after loading scripts | ~83 KiB (64-bit host; expect less on 32-bit SH4) | `BattleCtx` ≈ a few KiB, static | |
| Romdisk script payload | 39.6 KB live + 5.3 KB dead `.lua` | 0 | |
| Sprite slots / VRAM for `plasma.png` | 4 of 16 slots, 4 uploads | 1 slot | VRAM saved is only 6 KiB; the slots matter more |
| Input lookup | `strcmp` chain per `input.down("name")` call | `switch` on enum | |

**Interpretation.** At today's content size, the Lua battle logic is not a frame-rate bottleneck. I can't measure it on hardware here; my guess is a few percent of the 16.7 ms frame budget on the 200 MHz SH4, possibly more. To confirm, wrap `update`/`draw` in `timer_us_gettime64()` in the current build. The concrete wins are:
* **no GC pauses** as content grows (more actors and effects means more garbage)
* **fixed, known memory use** on a 16 MiB machine
* **compile-time checks** on typos in field names and string ids, which Lua only reveals at runtime as a silent `nil`
* one language and one debugger

---

## 8. Deliverables in `port/`

```
REPORT.md                     this document
results/                      raw logs: suite_f64.log, suite_f32.log, mutation.log
makefile                      KOS build (no -llua) + `make host` for the harness binaries
romdisk/                      PNGs only
src/                          portable game code, one .cpp per Lua module
  common.h engine.h battle_state.h
  battle combat status targeting turn_system skills encounters
  presentation fx hpbar ui input_probe assets (.h/.cpp)
platform/kos/main.cpp         Dreamcast backend (original renderer, Lua removed)
platform/host/                trace backend + driver used by the harness
platform/bench/bench_main.cpp host timing of the C++ port (no-op engine)
harness/
  lua_ref.py                  runs the ORIGINAL zip/romdisk scripts under Lua 5.5 and records traces
  gen_inputs.py               scenario + fuzz input generator (3 timing modes)
  compare.py                  per-frame, per-call trace comparison with tolerances
  run_suite.sh                full equivalence suite
  mutate.py                   mutation testing of the harness
  bench_lua.py                Lua garbage/CPU measurement
```

**Reproduce:**
```sh
python3 -m venv .venv && .venv/bin/pip install lupa     # bundles Lua 5.5
make host
PY=.venv/bin/python harness/run_suite.sh build/battle_trace_f64 1e-4 0 40   # expect pass=138 fail=0
PY=.venv/bin/python harness/run_suite.sh build/battle_trace_f32 0.01 1 40   # expect only jitter-mode 1-frame diffs
WORK=/tmp/suite python3 harness/mutate.py                                    # after run_suite
```

## 9. Recommended next steps
1. Build `port/` with DreamSDK and compare against the Lua build on Flycast and hardware. The expected differences are only the turn-order tie-break and the working B/Y buttons.
2. Keep `harness/` in the repo until the Lua files are deleted, then keep the recorded traces as golden regression tests for the C++ build.
3. Fix §5 items 1–3 first, each as its own change with re-recorded traces.
4. Move gameplay timers to integer ticks (§6.2) and add test-only encounters (§6.3).
5. If designers need to edit content without recompiling later, use data files loaded at boot (skills/encounters tables) rather than bringing back a scripting VM.
