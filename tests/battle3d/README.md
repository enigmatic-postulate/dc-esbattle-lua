# tests/battle3d: the 3D battle's checks (slice 3D-4)

Run everything with `tests/battle3d/run.sh` (`QUICK=1` for a shorter pass; `BPY=` and `SH4=1` add the
asset rebuild and the SH4 budget). What each check proves: `docs/SLICE_3D_BATTLE.md` §5.

| File | What it is |
|---|---|
| `run.sh` | the suite |
| `baseline/*.log`, `policies.txt`, `record_baseline.sh` | turn logs of 60 bot battles recorded from the untouched sprite build; the 3D build must reproduce them (`--rules legacy`) |
| `check_rules.py` | an independent Python model of the slice's rules (Guard fix, potion, burn, the Bone Warden's rotation and skills), checked against turn logs |
| `boss_balance.sh` | the GDD §12.6 balance bots (good / naive / no guard) over N seeds |
| `check_ui3d.py`, `ui3d.in` | UI rules on the 3D battle (`--write-input` regenerates the input) |
| `anim_dump.cpp`, `anim_check.py` | C++ animation sampling == the Python reference |
| `compare_dumps.py` | the view-culled props render the same pixels as the full ones |
| `cameras.sh` | the battle cameras of bot-played battles (input of `tools/battle3d/view_cull.py`) |
| `sh4_budget.sh` | the SH4 build under qemu: instructions (and, with `CACHE=1`, modelled time) per frame against the field's |

The bot (`platform/host/battle_bot.cpp`) plays through the real UI, closed loop, so the same policy
works on the sprite and the 3D battle. Policies are listed at the top of that file.
