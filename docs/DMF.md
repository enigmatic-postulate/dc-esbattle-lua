# DMF: Dreamcast Mesh Format (v2)

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

## v2: materials and shared textures (VS2)

A v2 file adds a **material table** (64 bytes per material, up to 8 per model), and each batch carries a material index in bits 8–15 of its flags. A material's texture is one of:

| Value | Meaning |
|---|---|
| `""` | untextured (vertex colour only): window glass, trees, the skeletons' glowing eyes |
| `"@"` | the texture embedded in the file (characters) |
| `"/rd/tex/wood.png"` | a **shared texture** on the romdisk |

Shared textures:
* are loaded once per path and per alpha mode (`dmf::shared_texture`);
* go through the same `png_to_img` path as the sprites;
* are referenced by many models: the village's wood-trim texture is used by 7 models but is in VRAM once.

Material flags:
* `MAT_BILINEAR`;
* `MAT_ALPHA`: ARGB1555 in the TR list. The vines use it.

v1 files still load: they have one implicit material.

## PRELIT (the VS1 art)

Header flag `PRELIT` (4, static models only; the loader rejects it on a skinned model): each vertex's `argb` is its
final lit colour, baked offline, and its normal is zero. `r3d::prelight()` then copies the colours instead of
lighting the normals (one colour set per model, whatever the yaw). The VS1 village props use it: VS1 baked
`0.50 + 0.62 * max(N.L, 0)` into them with a light a few degrees from ours (`tools/vs1art/import_props.py`,
[SLICE_VS1_ART.md](SLICE_VS1_ART.md)). Their `<name>_r<yaw>.dmf` variants are stored un-rotated, so the field draws
them like any other prop (`mat_trs_y(yaw)`).

**Conventions used by the field (not part of the file format):**
* `<name>_r<yaw>.dmf` (e.g. `house_stone_r180.dmf`) is the model for a `prop` placed at exactly that yaw, with only the faces the game camera can see there (view culling). The field uses it if present, else `<name>.dmf`. Since the VS1 art ([SLICE_VS1_ART.md](SLICE_VS1_ART.md)) every village prop is such a variant (VS1's camera-cone culling and baked light); the all-yaw house models were retired. There are no distance LODs.
* Static models get **per-strip bounding spheres** at load (`r3d::gfx_init`), so off-screen strips of a half-visible prop are neither transformed nor submitted.

## Layout (little-endian)

```
Header (96 B)   "DMF1", version 1|2, flags (SKINNED | TEXTURED | PRELIT), file_size, counts,
                bind-pose bounds, section offsets (v2: + num_materials, off_materials)
Materials (v2)  64 B each: char tex[48] ("" | "@" | "/rd/tex/x.png"), u16 flags
Vertices        32 B each: f32 x,y,z  f32 u,v  s8 nx,ny,nz  u8 w0  u32 argb  pad[4]
Skin groups     8 B each: u16 first, count; u8 bone0, bone1 (0xFF = none)
Strips          u16 words: [len, i0 .. i(len-1)] repeated; standard strip parity
Batches         16 B each: strip range + flags (DOUBLE_SIDED, TRANSLUCENT; v2: material in bits 8-15)
                + tri count
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
| `tools/dmf/build_village.py` | village props (v2, static, multi-material): assembles the Quaternius kit modules into prefabs, deletes invisible faces, decimates per part, writes LODs. Also writes the shared textures (`romdisk/tex/*.png`) and the ground atlas |
| `tools/dmf/make_placeholders.py` | cube / sphere placeholder models (static, vertex colours) |
| `tools/dmf/build_models.sh` | rebuilds every `romdisk/models/*.dmf`; byte-for-byte reproducible |
| `tools/dmf/dmf.py a.dmf` | validates and prints a summary |
| `tools/dmf/preview_dmf.py a.dmf out.png --anim walk --frames 8` | renders poses straight from the file (reference math + a PVR-like rasterizer) |

## Limits (v1)

* 65535 vertices per model.
* 32 bones (`dmf::MAX_BONES`).
* At most 2 bone influences per vertex.
* One embedded texture per model; up to 8 materials (v2).
* Translation animated on one bone (the hips).
* No per-vertex UV animation.
* The runtime transforms up to 4096 vertices per model (`r3d::MAX_XV`).
