# SH4ZAM slice: the SH4 hardware math layer

**Goal.** Move the hot-path math (matrices, trigonometry, quaternions, vectors) onto SH4ZAM, the
MIT-licensed SH4 math library (github.com/gyrovorbis/sh4zam), following its AGENTS.md: link it,
never copy it. No inline assembly in the project. The scene and the game content do not change,
and a plain `make` in DreamSDK still produces `battle.elf`.

---

## 1. Summary

| Item | Status |
|---|---|
| SH4ZAM integrated as AGENTS.md asks | **Done.** Linked as a library: the KallistiOS port by default (DreamSDK's "KallistiOS Ports" tab), or a full upstream checkout with `make SH4ZAM_DIR=<path>`, built by SH4ZAM's own CMake project. Nothing from SH4ZAM is copied into `src/` (§2) |
| Hot paths on SH4ZAM | **Done:** vertex transforms, matrix products, skinning, camera, culling, perspective divide, trig, quaternion sampling/blending, vectors, plus SH4ZAM's cache routines for the vertex buffers (§3) |
| No inline assembly | **Done:** `src/`, `platform/` and the makefile contain none; the only assembly is inside SH4ZAM (`grep` in §5.5) |
| Game logic unchanged | **Verified:** 44/44 host logic traces byte-identical to the pre-SH4ZAM build, and 22/22 traces of the SH4 build under qemu (§5) |
| Scene and content unchanged | **Verified:** `romdisk/` untouched. Rendered 3D output of the SH4 build within 0.113 px of the original on screen, same UVs, colours within 1/255; every strip culled differently draws no pixel (§5.3) |
| Builds with DreamSDK's `make` | **Checked as far as possible without DreamSDK:** the unmodified makefile, run in a KOS-shaped environment with the same GCC 13 and the exact flags of the user's last build, compiles everything, links `-lsh4zam` from `$KOS_PORTS/lib` and produces `battle.elf`; the `SH4ZAM_DIR` route too (§5.5) |
| Faster | **Measured on the SH4 code (qemu):** median field frame **1.44× fewer instructions** (1,120k → 777k), worst frame 1.68M → 1.17M; **FDIV/FSQRT per frame 8,912 → 23**; pipeline-model cycles 1.49× fewer. On top of that, cache-line allocation and prefetching that only the hardware can show (§4) |

Not done here: a run on a Dreamcast. The frame-time probe for that is already in the build:
`make DIAG=1` prints the worst CPU frame and the number of frames over 16.7 ms every 300 frames.
Compare it before and after this change (§4.4).

---

## 2. How SH4ZAM is integrated

SH4ZAM's AGENTS.md allows three ways: the KallistiOS port, a git submodule built by SH4ZAM's own
build system, or a whole-library synchronized copy. Copying individual routines is ruled out. This
project uses the first two and copies nothing:

* **Default: the KallistiOS port.** Install it once (DreamSDK Manager > KallistiOS Ports > SH4ZAM,
  or `cd $KOS_PORTS/sh4zam && make install`). Headers land in `$(KOS_PORTS)/include/sh4zam`
  (already on the include path) and the makefile adds `-lsh4zam`. If the port is missing, `make`
  stops at once with these instructions (`sh4zam-check`).
* **Pinned version: an upstream checkout**, e.g.
  `git submodule add https://github.com/gyrovorbis/sh4zam.git external/sh4zam`, then
  `make SH4ZAM_DIR=external/sh4zam`. The makefile builds it out of tree with SH4ZAM's CMake project
  (`kos-cmake ... -DSHZ_ENABLE_TESTS=OFF`, library target only) into `build/sh4zam-kos/` and links
  that archive.
* **Host harness** (`make host`, `tests/run_tests.sh`): SH4ZAM's portable software back-end, built
  from a checkout with SH4ZAM's CMake project into `build/sh4zam-host/`. The checkout is found at
  `SH4ZAM_DIR`, `external/sh4zam` or `../sh4zam/sh4zam` (where it sits in the delivered tree).
* **Version:** 0.10.0 or newer (the XMTRX 3x4 transpose family arrived in 0.9.0, the GL-style
  transform names in 0.10.0). `src/hwmath.h` stops the build with instructions if the headers are
  missing or older; the boot log prints the linked version and flags a header/library mismatch.
* **Flags:** release builds define `NDEBUG`, as SH4ZAM recommends (its debug asserts check
  alignment inside the hot loops; `make SHZ_DEBUG=1` keeps them). The three 3D hot translation
  units (`render3d`, `field3d`, `dmf`) build at `-O3`, everything else at the KOS default, as in
  SH4ZAM's optimisation guide (about 5% fewer instructions; `make HOT_OPT=` turns it off). No
  `-ffast-math`: `dmf.cpp` relies on NaN checks to reject corrupt files, and SH4ZAM emits FSCA and
  FSRRA itself without it.

`src/hwmath.h` is the one place that includes SH4ZAM. `src/math3d.h` follows SH4ZAM's "interop"
pattern: the renderer keeps its own types and names (`Vec3`, `Quat`, `Mat34`, `mat_mul`,
`mat_point`, `normalize`, ...) and each of them forwards to SH4ZAM.

* `Mat34` is 12 floats, row-major: exactly what SH4ZAM's `*_transpose_3x4` XMTRX routines read and
  write as rows 0-2 of the SH4's matrix register bank. A matrix goes in and out of XMTRX with no
  reshuffling.
* `Quat` is `shz::quat` (stored W, X, Y, Z); the `.dmf` data is X, Y, Z, W and goes through
  `quat_xyzw()`.
* `Vec3` converts to `shz_vec3_t` component by component, for free, since the values live in FP
  registers. It is never converted by pointer cast: see §6.1.

**XMTRX rule.** XMTRX is scratch state. Code that transforms a batch loads it first and must not
call anything that reloads it (`mat_mul`, `mat_look_at`, `emit_poly`, ...) until the batch is done.
The one-off helpers (`mat_point`, `mat_dir`, `mat_trs_y`, `quat_nlerp`) never touch it.

---

## 3. What runs on SH4ZAM

| Where | Before | Now (SH4ZAM, SH4 instruction) |
|---|---|---|
| Prop vertices (`draw_static`) | 12 multiply-adds + FDIV per emitted vertex | XMTRX = screen·world once per prop; **FTRV** + **FSRRA** per vertex |
| Strip culling (props) | 1-4 plane dot products per strip | **FTRV** of the strip's sphere centre (XMTRX already holds the prop's matrix), then one compare per screen edge |
| Character vertices (`draw_model`) | 12 multiply-adds + FDIV; two-bone groups twice | XMTRX = bone matrix once per skin group; **FTRV** + **FSRRA** per vertex and bone |
| Bone matrices (per frame) | `mat_mul` ×2 per bone in C | `shz_xmtrx_load/apply/store_transpose_3x4` (**FTRV**-based products); the viewport is folded into one camera matrix |
| Skinning (`pose_skin`) | quaternion → matrix + 2 `mat_mul` per bone | `shz_xmtrx_init_rotation_quat` / `translate` / `rotate_quat` / `apply`, all in the register bank |
| Animation sampling and blending | normalised lerp with `sqrtf` + FDIV | `shz_quat_nlerp` (**FSRRA**), `shz_vec3_lerp` |
| Box/sphere culling, fade test | 8 × 2 `mat_point` per prop | XMTRX = view·world (or proj·world); 8 **FTRV** |
| Camera (`camera_look`) | `tanf`, `sinf`/`cosf`, C matrix code | `shz_tanf_deg`, `shz_sincosf_deg` (**FSCA**), `shz_xmtrx_init_lookat`, XMTRX products |
| Ground grid, blob shadows, near-plane clipper | C matrix code + FDIV per point | one XMTRX load per row / shadow / polygon; **FTRV** + **FSRRA** per point |
| Trig and rounding (yaw, pitch, buckets, animation time) | `sinf`, `cosf`, `fmodf`, `floorf`, `ceilf`, `sqrtf` | `shz_sincosf` (**FSCA**), `shz_fmodf`, `shz_floorf`, `shz_ceilf`, `shz_sqrtf` (**FSRRA**), `shz_divf` |
| Vectors | C structs | `shz_vec3_add/sub/scale/dot/cross/lerp/normalize_safe`, `shz_dot6f/8f`, `shz_mag_sqr3f` |
| Vertex buffers (arena, transform scratch) | plain stores: each 32-byte vertex line first read from RAM | **MOVCA.L** (`shz_dcache_alloc_line`) claims each line, since all 32 bytes are written |
| Model vertex streams | read on demand | **PREF** (`SHZ_PREFETCH`) two vertices ahead |
| 2D battle (sprite wobble, bob, lunge, fades) | libm `sin`, `sqrt`, `floor` | `src/fastmath.h`: SH4ZAM on float builds; the host's double-precision equivalence build keeps libm |
| KOS backend (rotated sprites) | `fsin` + `fcos` (two FSCA) | `shz_sincosf` (one FSCA) |

Work removed as a side effect, because it was dead: with baked lighting (every enemy, and the hero
by default) the per-bone light vectors and the 256-entry shade table were still computed for every
model every frame; now only unbaked models compute them.

Kept on libm on purpose, with a comment in the code:
* load-time culling bounds (`dmf::parse`, strip spheres): exact `sqrtf`, since a bound must not
  come out short;
* the hero's facing target, `atan2f`, once per tick. A U-turn puts the target exactly π away, and
  the last bit of that angle decides which way the knight spins; SH4ZAM's fast approximation
  (about 1e-3 rad) would flip the spin on exact reversals.

---

## 4. Performance

### 4.1 Method

The game's SH4 code was compiled like the DreamSDK build (GCC 13, `-ml -m4-single -O2
-fomit-frame-pointer ...`, the flags in the DWARF of the user's last `battle.elf`) and run under
qemu-sh4 with an instruction-counting plugin (`tests/sh4zam/`). The engine is a no-op, apart from a
vertex arena that behaves like the KOS backend's. The same input scripts drive both builds, so the
game states, and therefore the frames, correspond one to one. The baseline is the pre-SH4ZAM tree at
its original `-O2`. Only 3D field frames are counted: 35,067 frames from the 9 field scripts
(`field_walk`, `field_full`, `field_win`, `field_lose`, `field_fuzz1`-`5`).

The baseline numbers match the 3D slice's own figures (median about 1.2M, worst about 1.5-1.7M
instructions; `docs/SLICE_3D_2.md` §4), measured with a different harness.

### 4.2 Results

| Per field frame (tick + draw) | Baseline | SH4ZAM | |
|---|---:|---:|---:|
| Median instructions | 1,120,333 | 776,519 | **1.44×** |
| 90th percentile | 1,298,807 | 906,635 | 1.43× |
| Worst frame | 1,677,747 | 1,174,379 | 1.43× |
| Model cycles, median (below) | 1,208,932 | 813,625 | **1.49×** |
| FDIV + FSQRT | 8,912 | 23 | |
| FTRV / FSRRA / FSCA | 0 / 0 / 0 | 12,471 / 8,191 / 8 | |
| FP arithmetic (FADD, FSUB, FMUL, FMAC) | 177,584 | 46,462 | 3.8× |
| FP loads and stores | 168,462 | 98,501 | 1.7× |

Per script, the median ratio is 1.41-1.45×. Per area (average field frame of `field_walk`):

| Area | Baseline | SH4ZAM | |
|---|---:|---:|---:|
| Props (`draw_static`: transform, strip culling, emission) | 468,280 | 309,406 | 1.51× |
| Characters (`draw_model`) | 459,475 | 345,771 | 1.33× |
| Ground grid | 113,482 | 90,317 | 1.26× |
| Blob shadows | 37,526 | 24,849 | 1.51× |
| Poses (sample, blend, skin) | 74,259 | 35,890 | 2.07× |
| Per-tick view update (camera follow, fade test) | 6,085 | 4,426 | 1.37× |
| **Total** | **1,180,583** | **826,214** | **1.43×** |

Inner loops, in SH4 instructions per vertex (read from the two binaries): prop vertex 61, one of
them an FDIV → 40, including FTRV, FSRRA and the MOVCA.L; skinned vertex with baked colours 53,
one an FDIV → 38, including the prefetch; strip copy 21 → 24 (the MOVCA.L and two constant words,
in exchange for not reading each destination line from RAM).

### 4.3 What instruction counts do not show

qemu counts instructions. It models neither the SH4's pipeline nor its caches, so:

* **Long FPU operations are undercounted in the baseline.** An FDIV or FSQRT occupies the FPU for
  about 10 more cycles and an FTRV for about 3. "Model cycles" above adds just that, nothing else:
  no dual issue, no stalls on results, no cache misses. In that model the median frame goes from
  6.0 to 4.1 ms of CPU at 200 MHz.
* **Cache effects (hardware only).** Every vertex buffer is far larger than the SH4's 16 KiB operand
  cache: the 512 KiB PVR arena, the 128 KiB transform scratch, and the model data itself. Each
  vertex is one 32-byte line. A plain store to a line not in the cache first reads it from RAM. The
  MOVCA.L before each vertex avoids that read on every vertex written: over 10,000 lines per field
  frame (the arena's vertices plus the characters' transformed vertices). The PREF hides the read of the next model vertices. These cost about 5% more instructions,
  included in the counts above, and are a net gain only on real hardware.

### 4.4 Frame rate

The loop presents at most 60 frames per second: fixed 60 Hz ticks, `pvr_wait_ready()` and a vblank
wait per frame. So the gain shows as frame rate wherever frames were missing the 16.7 ms budget:
the busy village views the 3D slice estimated at 11-15 ms. They fall back to 30 fps when the CPU
work, cache misses and store-queue traffic exceed the budget. Everywhere else it shows as headroom.
To see it on the hardware, build with `make DIAG=1` before and after this change. Every 300 frames
the serial log gives "worst CPU frame ... us, frames over 16.7 ms: N" and the PVR's frame rate.

---

## 5. Verification

### 5.1 Host suite (`tests/run_tests.sh`)

Same result as the untouched pre-SH4ZAM tree: **32 pass, 22 fail, the same 22**. They fail before
this change too. The battle and field goldens predate the 3D slice (they lack its model-load lines,
and `field_full` expects battles the village map does not have). P2 depends on the local PIL build.
T1 (0 warnings with `-Wall -Wextra -Wshadow`) passes with SH4ZAM's headers. Two harness fixes:

* the T1 check compiles as C++17 with SH4ZAM's headers, taken from `make print-host-shz-flags`;
* **P2 no longer changes the game's content.** It ran `tools/make_assets.py` in place, so with a
  different PIL it silently rewrote `romdisk/ui_icons.png` (this happened during this work and was
  reverted). It now restores the committed PNGs after comparing.

### 5.2 Game logic

* **Host:** all 22 input scripts, float and double builds: the `--state-only` traces (every tick's
  mode, positions, HP, statuses, turn order) are **byte-identical to the pre-SH4ZAM build, 44/44**.
  The full draw traces of every battle script are identical too.
* **SH4 (qemu, SH4ZAM's SH4 back-end):** the same traces, **22/22 byte-identical**.

### 5.3 Rendered output (`tools/compare_frames.py`)

Every 3D vertex of selected frames, SH4 build (qemu) against the original build on the host,
8 runs, about 4.2 million vertices. Frames were taken every 40-100 ticks, plus 60 frames in which a
prop crosses the near plane (the clipper path):

| | Result |
|---|---|
| Screen position, vertices on or near the screen | max **0.113 px**, median 0.05 px |
| Vertices far off screen (at the near plane) | relative error ≤ 1.6e-3 |
| Depth (1/W) | ≤ 1.7e-5 relative |
| UV | identical (≤ 2.4e-6) |
| Colour | identical, except 14 vertices at 1/255 |
| Strips drawn by one build only | 148, **every one covering 0 pixels** (rasterised against the pixel centres) |

The 0.05-0.11 px comes mostly from FSCA: it takes angles in 65536ths of a turn, so the camera's 20°
half field of view comes out 0.03% narrow (a 0.1 px scale at the screen edges). The strips drawn by
one build only lie wholly off screen, or at most 0.1 px inside an edge: rounding in a culling test
(or in the ground-grid bounds) lands on the other side. The SW back-end on the host is within
0.0003 px of the original.

### 5.4 Functions

`math3d.h` against the original implementations, 2,000 random cases each, on both back-ends:
`dot`, `cross`, `normalize`, the operators, `lerp`, `mat_mul`, `mat_point`, `mat_dir_t`,
`xmtrx_point`, `mat_look_at`, `quat_nlerp`, `mat_from_qt`, `angle_diff`. Largest relative error
1e-6, except `mat_trs_y`'s rotation under FSCA: 1.04e-4, the angle quantisation.

### 5.5 Build

* KOS-shaped environment (stub KOS headers and libraries, stub `kos-c++`/`kos-cmake` around GCC
  13.3 `sh-elf`, `KOS_CFLAGS` = the user's flags): the unmodified makefile builds all objects,
  links `battle.elf` with `-lsh4zam` from `$KOS_PORTS/lib` (a port-style install of the supplied
  SH4ZAM), and applies `-O3` to the three hot units only. `make SH4ZAM_DIR=<checkout>` builds
  `build/sh4zam-kos/libsh4zam.a` with SH4ZAM's CMake project and links it. Without the port, `make`
  stops with the install instructions. The real KOS headers were not available, so the KOS backend
  was type-checked against stubs.
* `grep -rnE '\basm\b|__asm' src platform makefile`: no assembly in the project. The only `.S`
  file is the emulator runtime in `tests/sh4zam/qemu-rt/`, which is test tooling.

---

## 6. Findings and caveats

### 6.1 A strict-aliasing bug found only on the SH4 path

The first version converted `Vec3` to `shz::vec3` with SH4ZAM's C++ interop helpers (`from()` and
`to<T>()`, which are `reinterpret_cast`s). The host build was correct. The SH4 build under qemu
culled the entire village from the 13th frame on: GCC 13 `-O2` (strict aliasing is on in the
DreamSDK build) moved a store to the camera's look-at point past a read through the `shz::vec3`
view. Fixes, and a rule for this code base:

* vectors convert component by component, which costs nothing since they live in registers;
* `Mat34`'s SH4ZAM view is a union member (`Mat34::rows`), so the compiler's alias analysis sees
  every XMTRX load and store that touches a matrix;
* the inverse bind matrices are copied out of the file image with `memcpy`, as before.

This is why the qemu harness exists. The host's SW back-end cannot exercise SH4 code generation.

### 6.2 Precision

FSCA (angle in 1/65536 of a turn) and FSRRA are approximations. All effects are sub-pixel (§5.3).
The animation blend weights, yaw buckets and fade decisions are cosmetic. None feeds the game
logic (§5.2).

### 6.3 For SH4ZAM upstream

Per AGENTS.md, these go upstream rather than into a fork. They are written up in
`tests/sh4zam/README.md`: `shz_memcpy32_1()`'s `"+m"` pointer constraint costs about 6
instructions per call in a loop; the C++ interop guide's `reinterpret_cast` helpers are exposed to
strict aliasing.

### 6.4 Not covered

* No run on a Dreamcast, and no frame-time measurement on hardware (§4.4 says how).
* The PVR submission (`pvr_prim`, store queues) is unchanged. Feeding SH4ZAM's store-queue routines
  straight to the TA would need the KOS SQ ownership and QACR setup checked on hardware.

---

## 7. Files

| File | Change |
|---|---|
| `src/hwmath.h` | **new**: the one include point for SH4ZAM, with the version and availability checks |
| `src/math3d.h` | math layer rewritten over SH4ZAM (interop types, XMTRX helpers) |
| `src/render3d.h/.cpp` | XMTRX/FTRV/FSRRA vertex pipeline, FTRV strip culling, MOVCA.L vertex writes, prefetch; camera has the viewport-folded `screen` matrix and precomputed frustum terms |
| `src/dmf.cpp` | skinning in XMTRX, `shz_quat_nlerp` sampling, `shz_fmodf` |
| `src/field3d.cpp` | camera trig, ground rows through XMTRX, fade test through XMTRX, SH4ZAM scalars |
| `src/fastmath.h` | **new**: `real`-typed SH4ZAM helpers for the 2D battle (libm on the double build) |
| `src/battle.cpp`, `ui.cpp`, `presentation.cpp`, `fx.cpp` | use `fastmath.h` |
| `platform/kos/main.cpp` | SH4ZAM version in the boot log; one FSCA per rotated sprite |
| `platform/host/host_engine.cpp`, `platform/bench/bench_main.cpp` | 32-byte-aligned vertex arenas, like the KOS backend's |
| `makefile` | SH4ZAM port / checkout, `sh4zam-check`, `NDEBUG`, `HOT_OPT`, host build against SH4ZAM's SW back-end |
| `tests/run_tests.sh` | C++17 + SH4ZAM headers for T1; P2 restores the committed assets |
| `tools/compare_frames.py` | **new**: vertex-level comparison of two builds' frame dumps |
| `tests/sh4zam/` | **new**: the SH4/qemu harness (§4.1, README) |

The build outputs that came with the tree (`*.o`, `*.d` with `C:/DreamSDK` paths, `battle.elf`;
git-ignored) were removed. The next `make` therefore rebuilds everything against the new headers,
which is needed anyway.
