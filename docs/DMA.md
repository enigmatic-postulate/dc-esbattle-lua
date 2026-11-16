# DMA: Dreamcast animation library (v1)

`.dma` holds skeletal clips shared by every model with the same skeleton. Every KayKit character
uses `Rig_Medium` (23 bones, identical rest transforms), so one copy of each clip serves the
Knight, the three skeletons, the Mage and the unconverted characters. Slice 3D-4 ships one
library: `romdisk/anims/rig_medium.dma` (28 clips, 252 KiB).

* **Writer and reference math:** `tools/battle3d/dmfio.py` (`write_dma`, `read_dma`, `sample_clip`).
* **Builder:** `tools/battle3d/build_anims.py` (Blender's Python): samples the packs' actions into the
  `.dmf` bone convention, detects clip events, writes a report (`tools/battle3d/reports/anims.json`).
* **Runtime:** `src/anim.h/.cpp` (loader/validator, name binding to a model, sampling, Animator).
* **Cross-check:** `tests/battle3d/anim_check.py`: C++ skin matrices == Python for every clip at six
  times on four models (worst difference 5.4e-6).

## Layout (little-endian, sections 4-byte aligned, file size a multiple of 32)

```
Header (64 B)    char magic[4] "DMA1"; u16 version (1); u16 num_bones; u32 file_size;
                 u16 num_clips; u16 reserved; u32 off_names; u32 off_clips; u8 pad[40]
Bone names       num_bones x char[16] (NUL-terminated): the library's bone order
Clips (64 B)     char name[20]; u16 num_frames; u8 flags (1 = LOOP); u8 num_tbones;
                 f32 fps; u32 t_mask (bit i: library bone i carries a translation);
                 u16 ev[4] (event ticks at 60 Hz, 0xFFFF = none); f32 bound[4] (centre xyz, radius
                 of the joints over the clip, model space); u32 off_data; u32 stride
Frame data       per frame: num_tbones x f32[3] translation (set bits of t_mask, ascending)
                 then num_bones x s16[4] quaternion x, y, z, w * 32767
                 stride = 12 * num_tbones + 8 * num_bones
```

## Semantics

* **Pose rule** (same as `.dmf`): `local = T(t) * R(q)`, `world = world(parent) * local`,
  `skin = world * inv_bind`. Bones a clip does not translate keep their bind translation.
* **Sampling:** frame `f = t * fps`; looping clips wrap (`num_frames` frames, the last one
  interpolating back to the first); one-shot clips clamp to their last frame. Quaternions nlerp
  (shortest path), translations lerp.
* **Root motion** lives in the hips: the builder keeps the `root` bone at identity and folds its
  motion into `hips` (root's only child). `Skeletons_Death` slides 0.7 units; `Dodge_Backward` 0.65.
* **Translated bones:** the hips always, plus any bone a clip moves away from its bind translation
  by more than 0.5 mm. `Skeletons_Death` moves the right leg 1.95 units (the skeleton falls apart)
  and `Skeletons_Awaken_Floor` assembles scattered bones: a hips-only format (`.dmf` v2 clips)
  cannot express either.
* **Binding:** a model's bones are matched to library bones by name (`anim::bind`); bones the
  library lacks keep their bind pose. All shipped characters match 23/23.
* **Events** (ticks from the clip start, integers so the rules gates are bit-reproducible):
  * `ev[0]` impact: the first frame after the weapon tip's speed peak where it has slowed to 35 %
    (the tip is 0.9 units along `handslot.r`);
  * `ev[1]` release: the right hand's speed peak (throws);
  * `ev[2]` collapse: the head within 5 cm of its lowest point (death clips).
  Overrides in `build_anims.py` `CLIPS` (one so far: `throw` release 46, detected 38 with the arm
  still rising).
* **Bound:** culling in the battle uses a sphere around the posed hips instead (`battle3d_draw.cpp`);
  the field value is kept for tools and future culling.

## Validity (runtime, `anim::parse`)

Magic, version and size; 1..32 bones; bone and clip names terminated; every clip: frames > 0,
0 < fps < 1000, `num_tbones == popcount(t_mask)` <= 12, mask bits < `num_bones`, the stride as above,
data in range, every translation finite with |x| < 1e4, bound finite. A rejected file is logged and
the battle draws nothing animated (it still plays: the rules never depend on the library).

## Clip list (rig_medium.dma)

| Library name | Source clip | Frames | Loop | Events | Used for |
|---|---|---:|:-:|---|---|
| idle_a | Idle_A | 32 | yes | | |
| hit_a, hit_b | Hit_A, Hit_B | 21, 27 | | | hit reactions |
| death_a, death_b | Death_A, Death_B | 25, 80 | | collapse 28, 102 | the hero's fall |
| throw | Throw | 42 | | release 46 | Fireball, the Mage's bolt, CHARGE (held), Grave Fire |
| use_item | Use_Item | 49 | | | Potion |
| interact | Interact | 40 | | | (dialog, later) |
| chop, slice_d, slice_h, jump_chop, stab | Melee_1H_Attack_* | 33, 31, 42, 41, 49 | | impact 38, 30, 22, 48, 26 | melee |
| chop_2h | Melee_2H_Attack_Chop | 50 | | impact 54 | Bone Slash, Raise Dead, Soul Rend, the phase slam |
| idle_2h, idle_melee | Melee_2H_Idle, Melee_Unarmed_Idle | 32, 32 | yes | | the Warden's and the hero's stances |
| block, blocking, block_hit | Melee_Block* | 33, 32, 33 | (blocking) | | Guard, Bone Ward |
| run | Running_A | 24 | yes | | run in / back |
| jump_short | Jump_Full_Short | 36 | | | victory |
| dodge_back, walk_back | Dodge_Backward, Walking_Backwards | 13, 32 | (walk) | | (spare) |
| skel_idle | Skeletons_Idle | 128 | yes | | skeleton idle |
| skel_awaken | Skeletons_Awaken_Standing | 31 | | | the battle intro |
| skel_rise | Skeletons_Awaken_Floor | 70 | | | Raise Dead (out of the dirt) |
| skel_death | Skeletons_Death | 61 | | collapse 26 | skeletons fall apart |
| skel_taunt | Skeletons_Taunt | 32 | | | defeat |

Not included: `Spawn_Air` / `Spawn_Ground` and the `EXPERIMENTAL` clip animate bone **scale**,
which neither format stores.
