# DMF: Dreamcast Mesh Format (v1)

`.dmf` is the model format for the 3D field. It is designed around the SH4 + PowerVR2 and has no dependencies.

* **Normative spec and reference implementation:** `tools/dmf/dmf.py` (writer, validator, reference pose/skinning math).
* **Runtime:** `src/dmf.h/.cpp` (loader/validator, animation sampling, skinning matrices) and `src/render3d.cpp` (transform, lighting, strips).
* The two are cross-checked vertex by vertex by `tests/slice3d/test_dmf.py`, on x86 and on SH4 under qemu.

## Design goals

| Goal | How |
|---|---|
| Load = one file read | sections are 32-byte aligned and structs point straight into the file image. No parsing, no heap. The whole image is validated first |
| SH4 cache friendly | vertices are 32 bytes (one cache line), floats are 4-aligned |
| Few matrix loads | vertices are pre-sorted into **skin groups** (runs that share the same bone pair), so a group loads its one or two bone matrices once. The Knight has 1611 vertices in 27 groups. This ordering is also what a future SH4 `FTRV` path needs (one XMTRX load per group) |
| TA-native geometry | **triangle strips** are pre-built by the converter (1.72 emitted vertices per triangle for the Knight). The TA takes strips with an end-of-strip flag, so no degenerate stitching is needed |
| Proven texture path | 16-bit texels are stored linearly (RGB565 / ARGB4444 / ARGB1555). The runtime uploads them like the PNG sprites (`kos_img_t` + `pvr_txr_load_kimg`, which twiddles them) |
| Small | Knight: 80.9 KB in total: vertices 51.6 KB, strips 6.2 KB, two clips 12.4 KB, skeleton 2.2 KB, texture 8.2 KB |

## Layout (little-endian)

```
Header (96 B)   "DMF1", version 1, flags (SKINNED | TEXTURED), file_size, counts,
                bind-pose bounds, section offsets
Vertices        32 B each: f32 x,y,z  f32 u,v  s8 nx,ny,nz  u8 w0  u32 argb  pad[4]
Skin groups     8 B each: u16 first, count; u8 bone0, bone1 (0xFF = none)
Strips          u16 words: [len, i0 .. i(len-1)] repeated; standard strip parity
Batches         16 B each: strip range + flags (DOUBLE_SIDED, TRANSLUCENT) + tri count
Bones           96 B each: name[16], s16 parent (< own index), bind t (3) + q (4) relative
                to the parent, inverse bind matrix (3x4)
Animations      32 B headers: name[16], frames, LOOP flag, translated bone, fps,
                ground_speed (units/s at 1x, for foot-locked playback), data offset
                data per frame: f32 t[3] + s16 quaternion x,y,z,w * 32767 per bone
Texture         32 B header (w, h, format, filter) + w*h u16 texels, top row first
```

**Pose rule (runtime == reference):**
* `local = T(t) * R(q)`; `world = world(parent) * local`; `skin = world * inv_bind`.
* `p' = w0 * skin(b0) * p + (1 - w0) * skin(b1) * p`.
* Clips are sampled by nlerp between frames. Looping clips wrap from the last frame to the first.

**Validity (both loaders):**
* every offset, count and index is in range;
* groups tile the vertices;
* parents precede children;
* every float the runtime reads is finite with |x| < 1e4.

The C++ and Python validators accept exactly the same files; 6000 mutated files were checked.

## Tools

| Tool | Use |
|---|---|
| `tools/dmf/kaykit_to_dmf.py` | KayKit character FBX (+ Mixamo clip) → `.dmf`. Runs in Blender's Python (`bpy`). It removes parts, deletes hidden faces (`--occlude`), decimates to a triangle budget with per-part weights, caps weights at 2 bones, retargets the clip, derives an idle, and downsamples the texture. Writes a JSON report |
| `tools/dmf/make_placeholders.py` | cube / sphere placeholder models (static, vertex colours) |
| `tools/dmf/build_models.sh` | rebuilds every `romdisk/models/*.dmf`; byte-for-byte reproducible |
| `tools/dmf/dmf.py a.dmf` | validates and prints a summary |
| `tools/dmf/preview_dmf.py a.dmf out.png --anim walk --frames 8` | renders poses straight from the file (reference math + a PVR-like rasterizer) |

## Limits (v1)

* 65535 vertices per model.
* 32 bones (`dmf::MAX_BONES`).
* At most 2 bone influences per vertex.
* One texture per model.
* Translation animated on one bone (the hips).
* No per-vertex UV animation.
* The runtime transforms up to 4096 vertices per model (`r3d::MAX_XV`).
