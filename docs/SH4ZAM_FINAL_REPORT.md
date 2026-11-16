---
title: "SH4ZAM Hardware Math Layer — Final Report"
subtitle: "larp-sh4zam (C++ field + battle, Sega Dreamcast / KallistiOS)"
---

# 1. Executive summary

This vertical slice puts the game's hot-path math on **SH4ZAM**, the MIT-licensed SH4 math
library: matrices, trigonometry, quaternions, vectors, and the memory traffic around them. The
game links SH4ZAM as a library, in the ways SH4ZAM's `AGENTS.md` allows, and calls its API. The
project contains no inline assembly. The scene and game content are unchanged.

The work was done in two passes:

* **Pass 1** was already in the delivered tree (documented in `docs/SLICE_SH4ZAM.md` §1–7). It
  moved all 3D vertex, matrix, skinning, camera, culling, trigonometry and quaternion math onto
  SH4ZAM, using XMTRX/FTRV, FSRRA, FSCA and MOVCA.L.
* **Pass 2** (this session) verified pass 1 independently on real SH4 code, then extended SH4ZAM's
  cache routines to the loops that index memory. Those loops had become the largest remaining
  cost on hardware. It also hardened the build.

Key results (median per 3D field frame, SH4 code compiled with the DreamSDK build's flags and run
under qemu):

| Measure | Before SH4ZAM | After pass 1 | After pass 2 |
|---|---:|---:|---:|
| SH4 instructions | 1,120,333 | 776,519 | 894,353 |
| FDIV + FSQRT | 8,912 | 23 | 23 |
| Cache misses (loads + stores) | not measured | 13,654 | **3,283** |
| Modelled CPU frame, 20-cycle miss | — | 5.48 ms | **4.99 ms** |
| Modelled CPU frame, 40-cycle miss | — | 6.85 ms | **5.32 ms** |

* Pass 1: **1.44× fewer instructions** and 1.49× fewer pipeline-model cycles than the pre-SH4ZAM
  build. FDIV/FSQRT dropped from 8,912 to 23 per frame.
* Pass 2: **4.47× fewer load misses** and **2.44× fewer store misses**, for 15% more (mostly
  dual-issuable) instructions. Modelled CPU frame time is **1.10× to 1.29× faster**, depending on
  the cost of a cache miss.
* Output is **bit-identical** to pass 1: every dumped vertex, every host trace, and every SH4
  logic trace.
* The makefile builds `battle.elf` in a KOS-shaped environment with the DreamSDK build's flags and
  no warnings. The SH4ZAM checkout route works, and a missing port stops with install
  instructions.

**Not done:** a run on a Dreamcast, and a build with the real DreamSDK/KOS headers. Neither was
available in this environment. §10 explains how to confirm the gain on hardware.

# 2. Requirements checklist

| Requirement | Status | Evidence |
|------------------------|------------------|--------------------------------------------|
| Use SH4ZAM for hot-path matrix, trig, quaternion and vector math | **Done** | §7 and §8: all 3D transforms, matrix products, skinning, camera, culling, perspective divide, trig, quaternion sampling/blending and vectors call SH4ZAM |
| Follow SH4ZAM's `AGENTS.md` (no copied routines) | **Done** | Linked as the KallistiOS port (`-lsh4zam`) by default, or a full upstream checkout built by SH4ZAM's own CMake project (`make SH4ZAM_DIR=…`). Nothing from SH4ZAM is copied into `src/` |
| No inline assembly in the project | **Done** | `grep -rnE '\basm\b|__asm' src platform makefile` finds nothing. FTRV, FSRRA, FSCA, MOVCA.L and PREF come only from SH4ZAM's API |
| `make` (DreamSDK) produces a valid `.elf` | **Verified as far as possible without DreamSDK** | KOS-shaped environment: GCC 13.3 `sh-elf`, the user's DreamSDK flags, stub KOS headers and libraries. The build is warning-free and links `battle.elf` (§6) |
| Scene and game content unchanged | **Verified** | `romdisk/` untouched; rendered output bit-identical to pass 1; game-logic traces identical |
| Large frame-rate increase, especially on hardware | **Measured on SH4 code, modelled for the cache** | Pass 1: 1.44× fewer instructions, FDIV/FSQRT almost eliminated. Pass 2: 75% fewer cache misses, 1.10–1.29× faster modelled frame. No hardware run |

# 3. How SH4ZAM is integrated

* **Default route: the KallistiOS port.** Install it once (DreamSDK Manager → *KallistiOS Ports*
  → SH4ZAM, or `cd $KOS_PORTS/sh4zam && make install`). The headers land in
  `$(KOS_PORTS)/include/sh4zam`, and the makefile adds `-lsh4zam`. If the port is missing, `make`
  stops at once with these instructions (`sh4zam-check`).
* **Pinned version: an upstream checkout**, e.g. as a git submodule, built with
  `make SH4ZAM_DIR=<path>`. The makefile builds it out of tree with SH4ZAM's own CMake project
  through `kos-cmake` into `build/sh4zam-kos/` and links that archive.
* **Host test harness:** SH4ZAM's portable software back-end, built from a checkout with its CMake
  project.
* **Version guard:** `src/hwmath.h` is the single include point. It requires SH4ZAM 0.10.0 or
  newer and prints instructions otherwise. The boot log prints the linked version.
* **Flags:**
  * release builds define `NDEBUG`, as SH4ZAM recommends;
  * the three hot translation units build at `-O3`;
  * pass 2 adds `-std=gnu++17`, because SH4ZAM's C++ headers need C++17 and GCC 9/10 default to
    C++14.
* **Interop pattern (`src/math3d.h`):** the game keeps its own types (`Vec3`, `Mat34`, `Quat`) and
  names, and each forwards to SH4ZAM.
  * `Mat34` is 12 floats, row-major: exactly the layout SH4ZAM's `*_transpose_3x4` XMTRX routines
    read and write.
  * Vectors convert component by component, never by pointer cast (§8, strict aliasing).

# 4. How it was verified

Pass 2 built an independent SH4 test environment, so the SH4 code path did not rest on pass 1's
claims.

| Tool | What it is |
|--------------------|-------------------------------------------------------|
| `sh-elf` GCC 13.3.0 | Built from Ubuntu's `gcc-13-source`, with binutils 2.40 and newlib 3.3, little-endian `-m4-single`. Same compiler family as DreamSDK's GCC 13.2 |
| qemu-sh4 10.1.0 + patch | Stock qemu cannot run SH4ZAM's SH4 back-end: FSCA is missing, FSRRA falls through to "illegal", and FIPR/FTRV have wrong bank, precision and index handling. `tests/sh4zam/qemu-10.1.0-sh4-fpu.patch` fixes these, and makes PREF visible to plugins |
| `shcount.c` (pass 1) | qemu plugin: SH4 instructions per tick and per draw, by class |
| `shcache.c` (pass 2, new) | qemu plugin: replays every load, store, MOVCA.L and PREF against a model of the SH4 operand cache (16 KiB, direct mapped, 32-byte lines, copy-back, as KOS configures it), with a per-instruction miss profile |
| `summarize_cache.py` (new) | Combines instruction and miss counts into modelled cycles at a 20- and a 40-cycle miss penalty |
| KOS-shaped build environment | Stub KOS headers, libraries and `kos-cc`/`kos-c++`/`kos-cmake` around the real `sh-elf` GCC, running the real makefile |

The benchmark runs the game's tick and draw with a no-op engine, except for a vertex arena that
behaves like the KOS backend's. The same 9 field input scripts (35,067 3D frames) drive every
build.

# 5. Performance results

## 5.1 Pass 1: math on SH4ZAM (from `docs/SLICE_SH4ZAM.md` §4)

| Per field frame | Pre-SH4ZAM | Pass 1 | |
|---|---:|---:|---:|
| Median instructions | 1,120,333 | 776,519 | **1.44×** |
| Worst frame | 1,677,747 | 1,174,379 | 1.43× |
| Model cycles (median) | 1,208,932 | 813,625 | **1.49×** |
| FDIV + FSQRT | 8,912 | 23 | |
| FP arithmetic | 177,584 | 46,462 | 3.8× |

The pass 2 harness reproduces pass 1's median exactly (776,519 instructions).

## 5.2 Pass 2: the memory side (all 35,067 field frames, medians)

| | Pass 1 | Pass 2 | |
|---|---:|---:|---:|
| Instructions | 776,519 | 894,353 | 0.87× |
| Load misses | 12,427 | 2,780 | **4.47× fewer** |
| Store misses (write allocation) | 1,227 | 503 | **2.44× fewer** |
| Write-backs | 15,252 | 15,266 | arena traffic, unchanged |
| PREF fills | 3,904 | 13,818 | |
| Model cycles, 20-cycle miss | 1,095,282 | 997,171 | **1.10×** |
| Model cycles, 40-cycle miss | 1,369,302 | 1,063,138 | **1.29×** |
| CPU frame at 200 MHz (20 / 40-cycle miss) | 5.48 / 6.85 ms | 4.99 / 5.32 ms | |

Per input script:

| Script | Frames | Instr. pass 1 | Instr. pass 2 | Misses pass 1 | Misses pass 2 | @20 | @40 |
|---|---:|---:|---:|---:|---:|---:|---:|
| field_walk | 2,276 | 783,678 | 903,175 | 13,590 | 3,272 | 1.09× | 1.27× |
| field_full | 3,760 | 742,498 | 856,563 | 12,780 | 2,985 | 1.09× | 1.28× |
| field_win | 1,920 | 779,371 | 897,037 | 14,280 | 3,247 | 1.10× | 1.30× |
| field_lose | 1,956 | 741,386 | 856,543 | 12,660 | 2,878 | 1.08× | 1.27× |
| field_fuzz1 | 1,155 | 857,045 | 987,894 | 15,544 | 3,409 | 1.10× | 1.31× |
| field_fuzz2 | 6,000 | 873,986 | 1,007,581 | 15,684 | 3,588 | 1.10× | 1.30× |
| field_fuzz3 | 6,000 | 738,102 | 849,316 | 12,940 | 3,124 | 1.09× | 1.28× |
| field_fuzz4 | 6,000 | 812,722 | 936,856 | 14,414 | 3,515 | 1.09× | 1.27× |
| field_fuzz5 | 6,000 | 717,967 | 825,756 | 11,224 | 3,152 | 1.06× | 1.21× |

Per area (`field_walk`, per frame, instructions / misses):

| Area | Pass 1 | Pass 2 |
|---|---:|---:|
| Characters (`draw_model`, incl. strip copy) | 338k / 6,798 | 409k / 700 |
| Props (`draw_static`) | 275k / 4,414 | 318k / 1,266 |
| Ground + per-instance loop (`field3d_draw`) | 68k / 777 | 70k / 486 |
| Poses (sample, skin, blend, bone `memcpy`) | 44k / 807 | 48k / 298 |
| Per-tick view update (`field3d_tick`) | 4k / 205 | 5k / 38 |

## 5.3 Interpreting the numbers

* **Miss cost:** the real cost of a miss depends on RAM timing and on how much latency a PREF
  hides, so pass 2 is given at 20 and 40 cycles per miss.
* **The model is conservative:** it charges one cycle per instruction with no dual issue. Most of
  pass 2's added instructions are MOV/ADD/PREF, which dual-issue with loads and stores on the SH4.
* **Frame-rate cap:** the game loop presents at most 60 frames per second. The gains show as
  frame-time headroom, and as 60 instead of 30 fps wherever a frame was over the 16.7 ms budget.
* **Not measured:** the KOS submission path (`pvr_prim`), which reads the arena back through the
  store queues, is unchanged and not included.

# 6. Verification results

| Check | Result |
|---------------------------|-----------------------------------------------|
| Rendered output, SH4 build under qemu, SH4ZAM's SH4 back-end | Every vertex of every 40th frame of 5 scripts (513 frames) **bit-identical** to pass 1 |
| Traces | Identical, except one boot-log line: the model arena grows 541 → 542 KiB (64-byte padding per model, §8) |
| Host full draw traces, float and double builds, all 22 scripts | Identical to pass 1 apart from that line |
| Game logic | SH4 `--state-only` traces equal the host's for **22/22** scripts |
| Host suite (`tests/run_tests.sh`) | 32 pass / 22 fail, the **same tests** as the untouched pass 1 tree run side by side. The 22 failures predate both passes: stale goldens from before the 3D slice, and a PIL-dependent asset test |
| Makefile, KOS-shaped environment | All objects compile with no warnings (`-Wall`). `-O3` reaches only `render3d`, `field3d` and `dmf`. `-lsh4zam` links from `$KOS_PORTS/lib` and `battle.elf` is produced |
| `make SH4ZAM_DIR=<checkout>` | Builds `build/sh4zam-kos/libsh4zam.a` with SH4ZAM's CMake via `kos-cmake`, and links it |
| SH4ZAM port missing | `make` stops with the DreamSDK / KOS install instructions |
| C++ standard | SH4ZAM's headers fail at `-std=gnu++14` and compile at gnu++17/20, hence the explicit `-std=gnu++17` |
| Inline assembly | None in `src/`, `platform/` or the makefile |

# 7. Files impacted by the SH4ZAM inclusion

## 7.1 Game source

| File | Pass | Role / change | SH4ZAM API used |
|----------------|--------|------------------------------------|------------------------------|
| `src/hwmath.h` | 1 (new) | Single include point for SH4ZAM; availability and version (≥ 0.10.0) checks; silences the library's style warnings | `shz_sh4zam.hpp`, `shz_version.h` |
| `src/math3d.h` | 1 | Renderer math layer rewritten over SH4ZAM: game types kept, zero-cost conversions, XMTRX helpers | `shz_vec3_add/sub/scale/dot/cross/lerp/normalize_safe/magnitude`, `shz_dot6f/8f`, `shz_xmtrx_load/apply/store_transpose_3x4`, `shz_xmtrx_transform_point3`, `shz_xmtrx_init_lookat`, `shz_xmtrx_init_rotation_quat`, `shz_xmtrx_set_translation`, `shz_quat_nlerp`, `shz_sincosf`, `shz_fmodf` |
| `src/render3d.h/.cpp` | 1 + 2 | Pass 1: FTRV/FSRRA vertex pipeline, FTRV culling, MOVCA.L vertex writes, viewport-folded camera matrix. Pass 2: PREF software pipelines in the strip loops; MOVCA.L + PREF for the two-bone scratch; PREF for baked colours, strip words and strip spheres | `shz_xmtrx_*`, `shz_invf_fsrra`, `shz_inv_sqrtf`, `shz_sqrtf`, `shz_tanf_deg`, `shz_sincosf`, `shz_lerpf`, `shz_mag_sqr3f`, `shz_dot6f`, `shz_fabsf`, `shz_dcache_alloc_line`, `shz_memcpy32_1`, `SHZ_PREFETCH` |
| `src/dmf.h/.cpp` | 1 + 2 | Pass 1: skinning in XMTRX, `shz_quat_nlerp` sampling. Pass 2: `LOOKAHEAD_PAD` zero bytes after each loaded model; PREF in `pose_sample` and `pose_skin` | `shz_xmtrx_init_rotation_quat`, `shz_xmtrx_translate`, `shz_xmtrx_rotate_quat`, `shz_xmtrx_set_translation`, `shz_fmodf`, `SHZ_PREFETCH` |
| `src/field3d.cpp` | 1 + 2 | Pass 1: camera trig, ground rows and fade test through XMTRX, SH4ZAM scalar functions. Pass 2: PREF in the per-instance loops | `shz_sincosf_deg`, `shz_floorf/ceilf/fmodf/fabsf/sqrtf/divf/divf_fsrra/invf_fsrra`, `shz_dcache_alloc_line`, `SHZ_PREFETCH` |
| `src/fastmath.h` | 1 (new) | `real`-typed helpers for the 2D battle: SH4ZAM on float builds, libm on the double-precision equivalence build | `shz_sinf`, `shz_sqrtf`, `shz_floorf` |
| `src/battle.cpp`, `ui.cpp`, `presentation.cpp`, `fx.cpp` | 1 | Enemy wobble, UI bob, lunge direction and fade rounding through `fastmath.h` | indirect |

## 7.2 Platform, build, tests and documentation

| File | Pass | Change |
|--------------------------|-------|---------------------------------------------|
| `platform/kos/main.cpp` | 1 | Linked SH4ZAM version in the boot log; rotated sprites use one `shz_sincosf` (a single FSCA) |
| `platform/host/host_engine.cpp`, `platform/bench/bench_main.cpp` | 1 | 32-byte-aligned vertex arenas (the `shz_memcpy32_1` / MOVCA.L contract) |
| `makefile` | 1 + 2 | Pass 1: SH4ZAM port or checkout, `sh4zam-check`, `NDEBUG`, `HOT_OPT=-O3`, host build on SH4ZAM's SW back-end. Pass 2: `-std=gnu++17` |
| `tests/run_tests.sh` | 1 | Warning check (T1) compiles as C++17 with SH4ZAM's headers; the asset test restores the committed PNGs |
| `tools/compare_frames.py` | 1 (new) | Vertex-level comparison of two builds' frame dumps |
| `tests/sh4zam/` | 1 + 2 | Pass 1: SH4/qemu harness (`sh4kit.sh`, `bench_sh4.cpp`, `shcount.c`, `summarize.py`, `profile.py`, `qemu-rt/`, qemu 10.0.13 patch). Pass 2: `shcache.c`, `summarize_cache.py`, `qemu-10.1.0-sh4-fpu.patch`, `sh4kit.sh cache` |
| `docs/SLICE_SH4ZAM.md` | 1 + 2 | Slice documentation (§8 covers pass 2) |
| `docs/SH4ZAM_FINAL_REPORT.md` / `.pdf` | 2 (new) | This report |

**Not affected:** `field.cpp`, `game.cpp`, `combat.cpp`, `skills.cpp`, `status.cpp`,
`turn_system.cpp`, `targeting.cpp`, `encounters.cpp` and the other game-logic files. They use
integer rules only and deliberately stay off SH4ZAM. `romdisk/` is unchanged.

# 8. Porting notes

Difficulty scale: **Easy** means a one-to-one substitution. **Moderate** means a data layout or
calling convention had to change. **Hard** means the change needed SH4-specific verification or
restructuring.

## 8.1 What was moved to SH4ZAM

| Method / hot path | SH4 technique | Difficulty | Notes |
|----------------|---------------|-------|------------------------------------|
| Vector operators (`+ - * dot cross normalize lerp length`) | SH4ZAM vec3 (FSRRA normalise) | **Moderate** | SH4ZAM's C++ conversion helpers are pointer casts. Under GCC `-O2` strict aliasing on SH4, a store moved past a read and the village was culled; the host build was fine. The fix converts component by component. Only running under qemu exposed it |
| `mat_mul`, bone and camera matrices | XMTRX products (FTRV) | **Moderate** | Row-major `Mat34` fits the `*_transpose_3x4` routines but not the faster fused `load_apply_store_3x4`. An "XMTRX is scratch" rule protects batches in progress |
| Vertex transform + perspective divide | FTRV + FSRRA | **Moderate** | FSRRA's divide returns 1/\|W\|, so W ≥ near_w had to be established on every path |
| Skinning (`pose_skin`) | Quaternion → matrix inside XMTRX; translate, rotate and apply there | Easy–Moderate | `.dmf` quaternions are stored X,Y,Z,W; SH4ZAM uses W,X,Y,Z (`quat_xyzw()` converts) |
| Animation sampling and blending | `shz_quat_nlerp`, `shz_vec3_lerp` | Easy | — |
| Camera, yaw and angle wrapping | FSCA `shz_sincosf(_deg)`, `shz_tanf_deg`, `shz_fmodf` | Easy | FSCA works in 1/65536 of a turn: field of view 0.03% narrow, at most 0.11 px on screen |
| Culling (sphere, box, per strip) | FTRV on corners and centres | **Moderate** | Sub-pixel edge decisions can flip; every strip that flips covers zero pixels |
| 2D battle cosmetics | `fastmath.h` | Easy | The double-precision equivalence build must keep libm |
| Vertex buffer writes | MOVCA.L (`shz_dcache_alloc_line`) | **Moderate** | Requires 32-byte alignment and every word of the line written |
| Strip loops (pass 2) | PREF software pipeline | **Hard** | Naive per-strip prefetch was a net loss (strips average ~4.5 vertices). The two-word lookahead past a model's last strip needed the `dmf::load()` padding contract. The benefit is invisible without the cache model |
| Two-bone skinning scratch (pass 2) | MOVCA.L + PREF | **Moderate** | A line is only claimed where both of its 16-byte entries are written before they are read |
| Poses and per-instance loops (pass 2) | PREF | Easy | — |

## 8.2 What could not be moved, or was deliberately left

| Item | Why |
|---------------------------|------------------------------------------------|
| Hero facing `atan2f` (`field3d_tick`) | `shz_atan2f` is approximate (~1e-3 rad), and the last bit decides the spin direction on an exact U-turn. It runs once per tick: no gain |
| Load-time bounds (`dmf::parse` radius, strip spheres) | A bound must not come out short, so exact `sqrtf` stays. Load time only |
| Double-precision host build | SH4ZAM is single precision only |
| Game rules (damage, turns, statuses) | Integer math by design |
| Colour packing (`shade()`), ground darkening, `dim()` | No SH4ZAM equivalent. Plain C with ordered compares, which avoid the soft-float unordered-compare call |
| Strip copy through `shz_memcpy32_1` | Measured in both passes: 32 vs 28 instructions per vertex. Its `"+m"` pointer constraint spills the source pointer through the stack |
| Two-bone blending with `shz_xmtrx_blend_3x4` | Weights differ per vertex: a per-vertex matrix blend plus XMTRX reload costs more than 2 FTRV + lerp |
| Fused `shz_xmtrx_load_apply_store_3x4` | Needs SH4ZAM's native layout, which would change `Mat34` and the `.dmf` inverse-bind data (content) |
| PVR submission (`pvr_prim`, store queues) | Owned by KOS. `shz_sq_memcpy32` would need KOS's SQ/QACR ownership validated on hardware |
| Baked-colour prefetch in `draw_static` | Measured neutral (+15k instructions for ~280 fewer misses), so left out |

# 9. Recommended additions to SH4ZAM

Each of these was checked against the SH4ZAM 0.10.0 headers and is not currently provided.
Following `AGENTS.md`, they belong upstream as issues or pull requests, not in a local copy.
Ordered by expected value for this build:

1. **Transform-and-project, single and batched.** E.g. `shz_xmtrx_transform_project_point3(p)`
   returning (X/W, Y/W, 1/W) via FTRV + FSRRA, and an array form with an output stride and
   built-in PREF/MOVCA.L. Every vertex loop in the game performs exactly this sequence. GCC spends
   about 7 instructions storing x/y/z (SH4 float stores have no offset addressing) and reloads
   constants per iteration. Vertex writing costs about 115k instructions per frame, so a
   hand-scheduled library routine would remove a large share of it. **Highest value.**
2. **Indexed (gather) variants.** Transform-and-project driven by a `uint16` strip index list, and
   a 32-byte gather copy with MOVCA.L and two-ahead PREF built in. These match the two largest
   remaining costs: prop emission (~318k instructions per frame) and the character strip copy
   (~190k). They would also make the lookahead padding contract unnecessary.
3. **Fix `shz_memcpy32_1`'s `"+m"` pointer constraint.** A performance fix rather than a feature:
   the library routine should then beat the project's hand-written word copy.
4. **Transposed fused product `shz_xmtrx_load_apply_store_transpose_3x4(out, a, b)`.** The
   pipelined fused routine for row-major 3x4 data, as used by SH4ZAM's own transpose family and by
   row-major engines. It would speed every bone and camera matrix product. Small gain (a few
   percent of the frame).
5. **Frustum helpers.** Sphere-versus-frustum and box-versus-frustum (8 corners through XMTRX),
   returning outside / inside / crossing and which edges are crossed. Used per object every
   frame. Mostly ergonomics, modest speed.
6. **Saturating colour pack** (float RGBA to ARGB8888, clamped without unordered-compare library
   calls). Only matters for per-frame dynamic lighting, which is off by default. Low priority.
7. **Quantised quaternion decode** (snorm16 keyframes, e.g. `shz_quat_from_snorm16`), ideally
   fused with `shz_quat_nlerp`. Used in pose sampling. Small gain.
8. **Documentation:** the C++ conversion helpers (`from()` / `to<T>()`) are `reinterpret_cast`s
   that strict aliasing can break under SH4 GCC at `-O2`, as happened in this project. Implement
   them with `__builtin_bit_cast`/`memcpy`, or document the hazard.

**Not recommended:** a precise `atan2f`. It is not a hot path, and libm is the right tool for that
tie-break.

Suggestions 1 and 2 target the largest remaining CPU costs; the others are incremental.

# 10. Caveats and next steps

* **No hardware run.** Measure on a Dreamcast with `make DIAG=1`, before and after. Every 300
  frames it prints the worst CPU frame, the number of frames over 16.7 ms, and the PVR frame
  rate.
* **No real DreamSDK/KOS build.** `platform/kos/main.cpp` was type-checked against stub KOS
  headers reconstructed from the KOS 2.x API. The real build also needs the SH4ZAM port installed
  (DreamSDK → *KallistiOS Ports*), version 0.10.0 or newer. Otherwise use
  `make SH4ZAM_DIR=<checkout>`.
* **Modelled, not measured, cache cost.** The operand-cache model does not include timing or
  instruction fetches. Results are therefore given for a 20- and a 40-cycle miss.
* **Arena write-backs are unchanged** (about 15k lines per frame). Removing them would mean
  building the frame straight into the TA, or using KOS's vertex DMA. That is a KOS-side change
  and needs hardware validation.

# Appendix: reproducing the measurements

```sh
export SH_ELF=~/shx QEMU_SH4=<qemu-10.1.0>/build/qemu-sh4 QEMU_SRC=<qemu-10.1.0> WORK=/tmp/sh4kit
tests/sh4zam/sh4kit.sh rt && tests/sh4zam/sh4kit.sh lib && tests/sh4zam/sh4kit.sh plugin
tests/sh4zam/sh4kit.sh bench . $WORK/bench_new.elf
tests/sh4zam/sh4kit.sh bench <older-tree> $WORK/bench_base.elf
for s in field_walk field_full; do
  tests/sh4zam/sh4kit.sh count $WORK/bench_new.elf tests/inputs/$s.in /tmp/cnt/$s.new.txt
  tests/sh4zam/sh4kit.sh cache $WORK/bench_new.elf tests/inputs/$s.in /tmp/cnt/$s.new.cache
  # ...the same for bench_base.elf into /tmp/cnt/$s.base.{txt,cache}
done
python3 tests/sh4zam/summarize_cache.py /tmp/cnt base new field_walk field_full
```

Full instructions, the toolchain requirements and the qemu patches are in
`tests/sh4zam/README.md`. The full technical write-up is `docs/SLICE_SH4ZAM.md`.
