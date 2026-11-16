# tests/sh4zam: running the game's SH4 code under an emulator

This directory is **test tooling only**: the game does not use it and the makefile never builds
it. It runs `src/` compiled exactly like the DreamSDK build (GCC 13, `-ml -m4-single -O2 ...`,
SH4ZAM's **SH4 back-end**: FTRV, FSCA, FSRRA, FIPR, MOVCA.L, PREF) under `qemu-sh4`, so that:

* the SH4 code path can be checked against the original code: logic traces byte for byte, and every
  3D vertex of selected frames (`tools/compare_frames.py`);
* the SH4 instructions each frame costs can be counted, per tick and per draw, and per function;
* the frame's data accesses can be replayed against a model of the SH4's operand cache, which is
  where SH4ZAM's cache routines (`SHZ_PREFETCH` = PREF, `shz_dcache_alloc_line` = MOVCA.L) act and
  what instruction counts cannot show.

The host harness (`tests/run_tests.sh`, `make host`) builds SH4ZAM's *software* back-end, which
is plain C; only this emulator run exercises the SH4 routines the Dreamcast executes. It found a
real bug the host could not see (see `docs/SLICE_SH4ZAM.md`, §6.1).

## Files

| File | What it is |
|---|---|
| `sh4kit.sh` | Driver: builds the pieces below and runs/counts ELFs (`sh4kit.sh` prints the usage) |
| `bench_sh4.cpp` | The game with a no-op engine, except a 3D vertex arena that behaves like the KOS backend's; one 60 Hz tick + one draw per input line, with marker calls around each |
| `shcount.c` | qemu TCG plugin: instructions per tick and per draw, by class (FDIV, FSQRT, FSRRA, FSCA, FTRV, FIPR, FP arithmetic, FP loads/stores, branches, integer loads/stores); optional per-block profile |
| `shcache.c` | qemu TCG plugin: operand-cache model (16 KiB, direct mapped, 32-byte lines, copy-back, as KOS sets it up). Per tick and per draw: load misses, store misses (write allocation), write-backs, MOVCA.L allocations, PREF fills; optional per-instruction miss profile |
| `summarize.py` | Baseline vs SH4ZAM table over the 3D field frames of some input scripts |
| `summarize_cache.py` | Two builds compared on instructions and cache misses, with modelled cycles for a 20- and a 40-cycle miss |
| `profile.py` | Instructions per function, from a `shcount.c` block profile |
| `qemu-rt/` | Runtime for running an `sh-elf` (newlib) program under `qemu-sh4` linux-user: newlib's low-level syscalls mapped onto Linux syscalls, and an entry point that sets KOS's start-up FPU mode (`__set_fpscr(0x40000)`). The one assembly file in the project lives here: a syscall trampoline and the entry point, for the emulator only |
| `qemu-10.0.13-sh4-fpu.patch` | Fixes to qemu's SH4 FPU decoder, needed to run SH4ZAM (below) |
| `qemu-10.1.0-sh4-fpu.patch` | The same fixes for qemu 10.1.0 (e.g. Ubuntu's `qemu` source package), which also needs FIPR/FTRV's vector index fixed, plus a measurement aid for `shcache.c`: qemu-user's PREF becomes a non-faulting one-byte load, so the plugin sees the prefetched address (stock qemu drops PREF at translation) |

## What you need

1. **An `sh-elf` GCC with C++ and newlib that supports `-ml -m4-single`**. The measurements in
   `docs/SLICE_SH4ZAM.md` used GCC 13.3 (the DreamSDK build uses 13.2), configured
   `--target=sh-elf --with-cpu=m4-single --with-endian=little --enable-languages=c,c++ --with-newlib`,
   against newlib 3.3 (Ubuntu's `libnewlib-sh-elf-dev`, `ml/m4-single` multilib). Point `SH_ELF`
   at its prefix. (Linux distributions' `sh4-linux-gnu` compilers are no use here: they are built
   without the `-m4-single` multilib, and glibc's `-m4` ABI leaves the FPU in double-precision mode
   after every call.)
2. **qemu-sh4 10.x with `qemu-10.0.13-sh4-fpu.patch` or `qemu-10.1.0-sh4-fpu.patch`**, configured
   with `--enable-plugins` (`--target-list=sh4-linux-user`; the cache model needs the 10.1.0 patch's
   PREF change). Stock qemu cannot run SH4ZAM's SH4 back-end:
   * `FSRRA` is decoded and then falls through to "illegal instruction" (`break` for `return`);
   * `FSCA` is not implemented at all (the Dreamcast's SH7091 has it); the patch adds it, exact
     (real hardware is an approximation, within `SHZ_FSCA_ERROR_MAX`);
   * `FTRV`/`FIPR` read the FR bank bit from SR instead of FPSCR (SH4ZAM swaps banks with FRCHG).
   qemu 8.x also rejects `FTRV`/`FIPR` outright (it tests FPSCR.PR the wrong way round), and so does
   10.1.0, which moreover uses the vector number as a register index (FV4 read as FR1..FR4).
   Ubuntu's `qemu_10.1.0+ds` source tarball lacks the firmware blobs and Python wheels: a
   linux-user build needs `pip install --user meson ninja pycotap`, `--disable-install-blobs`, and
   empty files for the `pc-bios` blobs its meson files list.
3. An SH4ZAM checkout (`SH4ZAM_DIR`; by default the same one the host build uses).

## Use

```sh
export SH_ELF=~/shx QEMU_SH4=~/qemu/build/qemu-sh4 QEMU_SRC=~/qemu WORK=/tmp/sh4kit
tests/sh4zam/sh4kit.sh rt && tests/sh4zam/sh4kit.sh lib && tests/sh4zam/sh4kit.sh plugin

# logic: the SH4 build's state trace must equal the host's
tests/sh4zam/sh4kit.sh trace . $WORK/trace.elf
tests/sh4zam/sh4kit.sh run $WORK/trace.elf tests/inputs/field_walk.in /tmp/sh4.trace --state-only
./build/battle_trace_f32 tests/inputs/field_walk.in /tmp/host.trace --state-only && cmp /tmp/sh4.trace /tmp/host.trace

# geometry: every 3D vertex of some frames, against another build's dump (e.g. the pre-SH4ZAM code)
tests/sh4zam/sh4kit.sh run $WORK/trace.elf tests/inputs/field_full.in /tmp/t --dump /tmp/sh4 --dump-frames 30,75,120
python3 tools/compare_frames.py /tmp/reference /tmp/sh4

# cost: per-frame instruction counts, this tree against an older one (built at its own -O2)
tests/sh4zam/sh4kit.sh bench . $WORK/bench_new.elf
HOT_OPT= tests/sh4zam/sh4kit.sh bench ../old-tree $WORK/bench_base.elf
mkdir -p /tmp/cnt
tests/sh4zam/sh4kit.sh count $WORK/bench_new.elf  tests/inputs/field_walk.in /tmp/cnt/field_walk.new.txt
tests/sh4zam/sh4kit.sh count $WORK/bench_base.elf tests/inputs/field_walk.in /tmp/cnt/field_walk.base.txt ../old-tree/romdisk
python3 tests/sh4zam/summarize.py /tmp/cnt field_walk

# cache: operand-cache misses per frame, the same two builds; then instructions + misses together
tests/sh4zam/sh4kit.sh cache $WORK/bench_new.elf  tests/inputs/field_walk.in /tmp/cnt/field_walk.new.cache
tests/sh4zam/sh4kit.sh cache $WORK/bench_base.elf tests/inputs/field_walk.in /tmp/cnt/field_walk.base.cache ../old-tree/romdisk
python3 tests/sh4zam/summarize_cache.py /tmp/cnt base new field_walk
```

`count` and `cache` print one line per frame to their output file: the counts of the tick, then of
the draw, in the class order given on the first line. Add `prof=FILE` to the plugin arguments (see
`sh4kit.sh`) for a per-block profile (`shcount.c`: instructions; `shcache.c`: misses per
instruction), then `NM=$SH_ELF/bin/sh-elf-nm CXXFILT=... python3 tests/sh4zam/profile.py ELF FILE`
(for a miss profile its "instructions" column reads as misses).

## What the numbers are, and are not

They are SH4 *instructions* executed by the game's tick and draw (the engine is a no-op, as the
KOS backend's submission cost does not depend on how the vertices were computed). qemu does not
model the SH4 pipeline or its caches, so:

* long FPU operations count as one instruction: an FDIV or FSQRT blocks the FPU for about 10 more
  cycles, an FTRV for about 3. `summarize.py`'s "model cycles" adds just that, and nothing else
  (no dual issue, no stalls, no cache misses);
* MOVCA.L (`shz_dcache_alloc_line`) and PREF (`SHZ_PREFETCH`) cost instructions here but save
  cache-line fills from RAM on the hardware, where they are free to measure: `make DIAG=1` prints
  the worst CPU frame time and the frames over 16.7 ms every 300 frames.

`shcache.c` adds the operand cache, as a model: it replays every load and store of the game (and
each MOVCA.L and PREF) against a 16 KiB direct-mapped, copy-back cache with 32-byte lines, and
counts the misses. It knows nothing of timing: a PREF counts as arriving in time, and the cost of a
miss is left to `summarize_cache.py`, which gives the totals for a 20- and a 40-cycle miss.
Instruction fetches (8 KiB instruction cache) are not modelled.

## Notes for SH4ZAM upstream

Per SH4ZAM's AGENTS.md, findings go upstream rather than into a fork:

* `shz_memcpy32_1()` (SH4 back-end): its load helper binds the source pointer variable with a
  `"+m"` constraint, which forces that pointer into memory; in a loop of single-block copies
  GCC 13 then spills and reloads it around every copy (about 6 extra instructions). The renderer's
  strip copy therefore uses word copies plus `shz_dcache_alloc_line()`.
* The C++ interop helpers (`shz::vecN::from()` / `to<T>()`, `SHZ_CONVERT` in C++) are
  `reinterpret_cast`s. Used on a separate, layout-compatible type they are type punning that GCC's
  strict-aliasing optimisation may reorder; with GCC 13.3 `-O2` on SH4 that happened in this
  project (a store to a `Vec3` moved past the read through the `shz::vec3` view). This project
  converts component by component instead; the interop guide could say so, or the helpers could
  go through `memcpy`/`__builtin_bit_cast`.
