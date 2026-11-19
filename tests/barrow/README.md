# tests/barrow - slice 3D-6 ("Into the Barrow") test suite

`tests/barrow/run.sh` (about 15 minutes; `QUICK=1` about 6) checks the plan's T1-T14
([docs/NEXT_SLICE_BARROW.md](../../docs/NEXT_SLICE_BARROW.md) §11) on the host harness, starting every route as the
Dreamcast does (`--boot`: the title):

| File | What |
|---|---|
| `routes/full.route` | T1: title -> T2 -> intro -> the village quest -> Corwin -> the barrow -> the waystone -> T8 -> the Warden -> the ending -> credits -> results -> T11 -> the title with its crest |
| `routes/barrow_back.route` | T2: into the barrow and back out (BA3), Brom's rest, back in, to the end: the same final quest state |
| `routes/cp_c3.route` | T4: lose to the Warden, the death screen, RETRY at the waystone (C3, byte for byte) with the short intro, win |
| `routes/rest_after_c3.route` | T4: Brom's rest after the waystone moves the checkpoint to the village: the boss's loss restores it there (no intro runs), back in, won |
| `routes/death_title.route` | T4: a loss -> RETURN TO TITLE -> a new game starts at S0 |
| `routes/road_block.route` | T5: the road block (walked back a tile, every time), the closed exit, the gate, the exits both ways (from `game_init`, no title) |
| `routes/intro_watched.route`, `skip_intro.route`, `skip_ending.route` | T6: watched == skipped (the intro; the boss intro, the ending and the credits) |
| `routes/idle_attract.route`, `routes/attract.route` | T11: 40 s idle starts the demo; it replays `attract.route`'s recording (`tools/make_attract.sh`) tick for tick |
| `check_flow.py` | every mode change is a catalogued transition with its exact ticks (T lines), along the GDD 3 flow (N lines) |
| `check_results.py` | the results card (the R line) against counters recomputed from the run and its turn log |

Also: the barrow tile sweep and the title / credits camera paths (`battle_trace --path title|credits`), every frame of
the playthrough by mode (T3), fog lines in the frame dumps (T8), resources and a 0-allocation run (T12), the assets'
byte-identical rebuilds (T14), ASan/UBSan, the KOS backend against stub headers in six flavours (T15), and the script
lint and C++ == Python parity (T7; the negative fixtures are `tests/dialog/lint_bad/`).
