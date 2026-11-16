# Character render QC (tools/dmf/qc_chars.py)

Source FBX (2 influences, before hidden-surface removal and simplification) vs the DMF, both posed
with the DMF's own keys. Gate per view: silhouette <= 0.5 %, unlit colour <= 1 % (a colour may
move by one QC pixel = half a game pixel). "strict" = the village metric (no shift), reported only.
Face rows are close-ups for review (not gated; faces were judged by eye, see
[SLICE_3D_NPCS.md](../SLICE_3D_NPCS.md) §3). Sheets: source | DMF | difference | DMF with the Dreamcast texture.

## hero: OK

`qc_chars.py --fbx Knight.fbx --texture knight_texture.png --dmf romdisk/models/hero.dmf` -> [hero.jpg](hero.jpg)

```
OK   hero.dmf: 2272 tris, 60 views, worst silhouette 0.34 %, worst bad px 0.23 % (strict, not gated: 1.64 %), worst face lit 0.65 %
```

| view | silhouette | colour | strict (info) | face lit (info) |
|---|---:|---:|---:|---:|
| bind / 45 front | 0.15 % | 0.00 % | 1.14 % |  |
| bind / 45 front-right | 0.13 % | 0.00 % | 0.88 % |  |
| bind / 45 back-right | 0.17 % | 0.01 % | 0.31 % |  |
| bind / 45 back | 0.08 % | 0.00 % | 0.04 % |  |
| bind / 45 back-left | 0.21 % | 0.01 % | 0.46 % |  |
| bind / 45 front-left | 0.19 % | 0.07 % | 1.39 % |  |
| bind / 29 front | 0.13 % | 0.10 % | 1.14 % |  |
| bind / 29 right | 0.29 % | 0.00 % | 0.43 % |  |
| bind / 61 left | 0.13 % | 0.05 % | 0.40 % |  |
| bind / 61 back | 0.06 % | 0.00 % | 0.00 % |  |
| bind / face 29 front (review) | 0.23 % | 0.00 % | 0.91 % | 0.10 % |
| bind / face 45 front (review) | 0.45 % | 0.08 % | 0.93 % | 0.62 % |
| bind / face 45 left (review) | 0.40 % | 0.12 % | 1.08 % | 0.62 % |
| bind / face 45 right (review) | 0.58 % | 0.09 % | 0.91 % | 0.12 % |
| walk t=0.14 / 45 front | 0.22 % | 0.13 % | 1.52 % |  |
| walk t=0.14 / 45 back-right | 0.29 % | 0.08 % | 0.46 % |  |
| walk t=0.14 / 45 back-left | 0.14 % | 0.01 % | 0.58 % |  |
| walk t=0.14 / 29 front | 0.15 % | 0.23 % | 1.32 % |  |
| walk t=0.14 / 29 right | 0.23 % | 0.13 % | 0.51 % |  |
| walk t=0.14 / 61 left | 0.24 % | 0.06 % | 0.36 % |  |
| walk t=0.14 / 61 back | 0.11 % | 0.01 % | 0.10 % |  |
| walk t=0.43 / 45 front | 0.11 % | 0.05 % | 1.23 % |  |
| walk t=0.43 / 45 back-right | 0.29 % | 0.05 % | 0.43 % |  |
| walk t=0.43 / 45 back-left | 0.11 % | 0.03 % | 0.49 % |  |
| walk t=0.43 / 29 front | 0.04 % | 0.07 % | 0.98 % |  |
| walk t=0.43 / 29 right | 0.34 % | 0.07 % | 0.58 % |  |
| walk t=0.43 / 61 left | 0.27 % | 0.04 % | 0.44 % |  |
| walk t=0.43 / 61 back | 0.09 % | 0.00 % | 0.05 % |  |
| walk t=0.72 / 45 front | 0.22 % | 0.15 % | 1.64 % |  |
| walk t=0.72 / 45 back-right | 0.18 % | 0.02 % | 0.30 % |  |
| walk t=0.72 / 45 back-left | 0.13 % | 0.04 % | 0.35 % |  |
| walk t=0.72 / 29 front | 0.23 % | 0.05 % | 1.11 % |  |
| walk t=0.72 / 29 right | 0.25 % | 0.02 % | 0.38 % |  |
| walk t=0.72 / 61 left | 0.18 % | 0.11 % | 0.70 % |  |
| walk t=0.72 / 61 back | 0.12 % | 0.00 % | 0.04 % |  |
| idle t=0.30 / 45 front | 0.04 % | 0.04 % | 1.13 % |  |
| idle t=0.30 / 45 back-right | 0.27 % | 0.02 % | 0.36 % |  |
| idle t=0.30 / 45 back-left | 0.16 % | 0.07 % | 0.44 % |  |
| idle t=0.30 / 29 front | 0.11 % | 0.06 % | 1.20 % |  |
| idle t=0.30 / 29 right | 0.29 % | 0.01 % | 0.41 % |  |
| idle t=0.30 / 61 left | 0.29 % | 0.10 % | 0.46 % |  |
| idle t=0.30 / 61 back | 0.09 % | 0.00 % | 0.11 % |  |
| idle t=0.90 / 45 front | 0.05 % | 0.05 % | 1.31 % |  |
| idle t=0.90 / 45 back-right | 0.27 % | 0.00 % | 0.35 % |  |
| idle t=0.90 / 45 back-left | 0.16 % | 0.06 % | 0.46 % |  |
| idle t=0.90 / 29 front | 0.13 % | 0.05 % | 1.18 % |  |
| idle t=0.90 / 29 right | 0.28 % | 0.02 % | 0.38 % |  |
| idle t=0.90 / 61 left | 0.28 % | 0.07 % | 0.45 % |  |
| idle t=0.90 / 61 back | 0.09 % | 0.00 % | 0.09 % |  |
| idle t=0.90 / face 29 front (review) | 0.23 % | 0.05 % | 0.86 % | 0.24 % |
| idle t=0.90 / face 45 front (review) | 0.43 % | 0.16 % | 1.38 % | 0.65 % |
| idle t=0.90 / face 45 left (review) | 0.41 % | 0.18 % | 1.12 % | 0.51 % |
| idle t=0.90 / face 45 right (review) | 0.58 % | 0.15 % | 0.98 % | 0.18 % |
| idle t=1.50 / 45 front | 0.06 % | 0.07 % | 1.38 % |  |
| idle t=1.50 / 45 back-right | 0.25 % | 0.02 % | 0.36 % |  |
| idle t=1.50 / 45 back-left | 0.16 % | 0.07 % | 0.43 % |  |
| idle t=1.50 / 29 front | 0.12 % | 0.04 % | 1.07 % |  |
| idle t=1.50 / 29 right | 0.27 % | 0.01 % | 0.37 % |  |
| idle t=1.50 / 61 left | 0.25 % | 0.07 % | 0.55 % |  |
| idle t=1.50 / 61 back | 0.09 % | 0.01 % | 0.08 % |  |

## skeleton: OK

`qc_chars.py --fbx Skeleton_Warrior.fbx --texture skeleton_texture.png --unlit Glow=78FFE6 --dmf romdisk/models/skeleton.dmf` -> [skeleton.jpg](skeleton.jpg)

```
OK   skeleton.dmf: 2499 tris, 60 views, worst silhouette 0.30 %, worst bad px 0.40 % (strict, not gated: 2.28 %), worst face lit 0.31 %
```

| view | silhouette | colour | strict (info) | face lit (info) |
|---|---:|---:|---:|---:|
| bind / 45 front | 0.15 % | 0.03 % | 0.99 % |  |
| bind / 45 front-right | 0.17 % | 0.30 % | 1.70 % |  |
| bind / 45 back-right | 0.16 % | 0.16 % | 1.79 % |  |
| bind / 45 back | 0.30 % | 0.14 % | 1.46 % |  |
| bind / 45 back-left | 0.21 % | 0.01 % | 1.10 % |  |
| bind / 45 front-left | 0.09 % | 0.01 % | 0.81 % |  |
| bind / 29 front | 0.15 % | 0.01 % | 1.14 % |  |
| bind / 29 right | 0.10 % | 0.32 % | 1.94 % |  |
| bind / 61 left | 0.18 % | 0.04 % | 1.54 % |  |
| bind / 61 back | 0.21 % | 0.40 % | 2.28 % |  |
| bind / face 29 front (review) | 0.23 % | 0.12 % | 1.23 % | 0.09 % |
| bind / face 45 front (review) | 0.27 % | 0.12 % | 0.97 % | 0.17 % |
| bind / face 45 left (review) | 0.22 % | 0.03 % | 1.58 % | 0.04 % |
| bind / face 45 right (review) | 0.11 % | 0.34 % | 1.79 % | 0.22 % |
| idle t=0.64 / 45 front | 0.14 % | 0.07 % | 1.04 % |  |
| idle t=0.64 / 45 back-right | 0.12 % | 0.35 % | 2.10 % |  |
| idle t=0.64 / 45 back-left | 0.23 % | 0.02 % | 1.58 % |  |
| idle t=0.64 / 29 front | 0.12 % | 0.02 % | 0.81 % |  |
| idle t=0.64 / 29 right | 0.10 % | 0.25 % | 1.80 % |  |
| idle t=0.64 / 61 left | 0.14 % | 0.14 % | 1.61 % |  |
| idle t=0.64 / 61 back | 0.23 % | 0.16 % | 1.63 % |  |
| idle t=1.92 / 45 front | 0.13 % | 0.10 % | 1.21 % |  |
| idle t=1.92 / 45 back-right | 0.13 % | 0.30 % | 1.98 % |  |
| idle t=1.92 / 45 back-left | 0.20 % | 0.04 % | 1.62 % |  |
| idle t=1.92 / 29 front | 0.10 % | 0.02 % | 0.76 % |  |
| idle t=1.92 / 29 right | 0.09 % | 0.25 % | 1.32 % |  |
| idle t=1.92 / 61 left | 0.09 % | 0.22 % | 1.75 % |  |
| idle t=1.92 / 61 back | 0.20 % | 0.29 % | 1.85 % |  |
| idle t=1.92 / face 29 front (review) | 0.11 % | 0.13 % | 1.20 % | 0.11 % |
| idle t=1.92 / face 45 front (review) | 0.35 % | 0.24 % | 2.07 % | 0.14 % |
| idle t=1.92 / face 45 left (review) | 0.34 % | 0.09 % | 2.13 % | 0.07 % |
| idle t=1.92 / face 45 right (review) | 0.28 % | 0.49 % | 2.59 % | 0.31 % |
| idle t=3.20 / 45 front | 0.12 % | 0.12 % | 1.26 % |  |
| idle t=3.20 / 45 back-right | 0.14 % | 0.30 % | 1.82 % |  |
| idle t=3.20 / 45 back-left | 0.15 % | 0.07 % | 1.68 % |  |
| idle t=3.20 / 29 front | 0.13 % | 0.02 % | 0.83 % |  |
| idle t=3.20 / 29 right | 0.10 % | 0.29 % | 1.37 % |  |
| idle t=3.20 / 61 left | 0.09 % | 0.31 % | 1.92 % |  |
| idle t=3.20 / 61 back | 0.19 % | 0.39 % | 1.85 % |  |
| walk t=0.24 / 45 front | 0.23 % | 0.13 % | 1.17 % |  |
| walk t=0.24 / 45 back-right | 0.15 % | 0.24 % | 1.78 % |  |
| walk t=0.24 / 45 back-left | 0.12 % | 0.08 % | 1.51 % |  |
| walk t=0.24 / 29 front | 0.12 % | 0.02 % | 0.73 % |  |
| walk t=0.24 / 29 right | 0.23 % | 0.27 % | 1.33 % |  |
| walk t=0.24 / 61 left | 0.27 % | 0.30 % | 1.72 % |  |
| walk t=0.24 / 61 back | 0.22 % | 0.39 % | 1.61 % |  |
| walk t=0.72 / 45 front | 0.18 % | 0.10 % | 1.11 % |  |
| walk t=0.72 / 45 back-right | 0.13 % | 0.24 % | 1.69 % |  |
| walk t=0.72 / 45 back-left | 0.10 % | 0.10 % | 1.56 % |  |
| walk t=0.72 / 29 front | 0.12 % | 0.02 % | 0.75 % |  |
| walk t=0.72 / 29 right | 0.17 % | 0.24 % | 1.19 % |  |
| walk t=0.72 / 61 left | 0.06 % | 0.26 % | 1.68 % |  |
| walk t=0.72 / 61 back | 0.21 % | 0.39 % | 1.84 % |  |
| walk t=1.20 / 45 front | 0.16 % | 0.08 % | 0.95 % |  |
| walk t=1.20 / 45 back-right | 0.09 % | 0.27 % | 1.66 % |  |
| walk t=1.20 / 45 back-left | 0.13 % | 0.08 % | 1.59 % |  |
| walk t=1.20 / 29 front | 0.06 % | 0.05 % | 0.92 % |  |
| walk t=1.20 / 29 right | 0.19 % | 0.25 % | 1.47 % |  |
| walk t=1.20 / 61 left | 0.18 % | 0.12 % | 1.69 % |  |
| walk t=1.20 / 61 back | 0.12 % | 0.39 % | 1.83 % |  |

## mage: OK

`qc_chars.py --fbx Mage.fbx --texture mage_texture.png --dmf romdisk/models/mage.dmf` -> [mage.jpg](mage.jpg)

```
OK   mage.dmf: 2998 tris, 60 views, worst silhouette 0.06 %, worst bad px 0.44 % (strict, not gated: 1.73 %), worst face lit 0.27 %
```

| view | silhouette | colour | strict (info) | face lit (info) |
|---|---:|---:|---:|---:|
| bind / 45 front | 0.01 % | 0.01 % | 0.36 % |  |
| bind / 45 front-right | 0.00 % | 0.00 % | 0.41 % |  |
| bind / 45 back-right | 0.00 % | 0.00 % | 0.06 % |  |
| bind / 45 back | 0.00 % | 0.00 % | 0.07 % |  |
| bind / 45 back-left | 0.02 % | 0.00 % | 0.06 % |  |
| bind / 45 front-left | 0.01 % | 0.00 % | 0.45 % |  |
| bind / 29 front | 0.01 % | 0.02 % | 0.36 % |  |
| bind / 29 right | 0.02 % | 0.00 % | 0.39 % |  |
| bind / 61 left | 0.02 % | 0.00 % | 0.13 % |  |
| bind / 61 back | 0.00 % | 0.00 % | 0.07 % |  |
| bind / face 29 front (review) | 0.01 % | 0.00 % | 0.32 % | 0.00 % |
| bind / face 45 front (review) | 0.07 % | 0.00 % | 0.11 % | 0.01 % |
| bind / face 45 left (review) | 0.03 % | 0.00 % | 0.06 % | 0.00 % |
| bind / face 45 right (review) | 0.04 % | 0.00 % | 0.04 % | 0.01 % |
| idle t=0.16 / 45 front | 0.04 % | 0.01 % | 0.57 % |  |
| idle t=0.16 / 45 back-right | 0.00 % | 0.00 % | 0.11 % |  |
| idle t=0.16 / 45 back-left | 0.03 % | 0.00 % | 0.14 % |  |
| idle t=0.16 / 29 front | 0.05 % | 0.04 % | 0.52 % |  |
| idle t=0.16 / 29 right | 0.04 % | 0.10 % | 0.50 % |  |
| idle t=0.16 / 61 left | 0.01 % | 0.02 % | 0.05 % |  |
| idle t=0.16 / 61 back | 0.00 % | 0.00 % | 0.08 % |  |
| idle t=0.48 / 45 front | 0.05 % | 0.02 % | 0.66 % |  |
| idle t=0.48 / 45 back-right | 0.00 % | 0.00 % | 0.10 % |  |
| idle t=0.48 / 45 back-left | 0.03 % | 0.00 % | 0.09 % |  |
| idle t=0.48 / 29 front | 0.05 % | 0.03 % | 0.62 % |  |
| idle t=0.48 / 29 right | 0.06 % | 0.07 % | 0.43 % |  |
| idle t=0.48 / 61 left | 0.00 % | 0.03 % | 0.06 % |  |
| idle t=0.48 / 61 back | 0.00 % | 0.00 % | 0.08 % |  |
| idle t=0.48 / face 29 front (review) | 0.02 % | 0.00 % | 0.16 % | 0.00 % |
| idle t=0.48 / face 45 front (review) | 0.07 % | 0.00 % | 0.11 % | 0.01 % |
| idle t=0.48 / face 45 left (review) | 0.08 % | 0.22 % | 0.52 % | 0.27 % |
| idle t=0.48 / face 45 right (review) | 0.04 % | 0.21 % | 0.43 % | 0.07 % |
| idle t=0.80 / 45 front | 0.05 % | 0.01 % | 0.61 % |  |
| idle t=0.80 / 45 back-right | 0.00 % | 0.00 % | 0.11 % |  |
| idle t=0.80 / 45 back-left | 0.03 % | 0.00 % | 0.15 % |  |
| idle t=0.80 / 29 front | 0.06 % | 0.04 % | 0.58 % |  |
| idle t=0.80 / 29 right | 0.04 % | 0.09 % | 0.46 % |  |
| idle t=0.80 / 61 left | 0.00 % | 0.02 % | 0.05 % |  |
| idle t=0.80 / 61 back | 0.00 % | 0.00 % | 0.08 % |  |
| walk t=0.16 / 45 front | 0.02 % | 0.06 % | 0.57 % |  |
| walk t=0.16 / 45 back-right | 0.00 % | 0.00 % | 0.13 % |  |
| walk t=0.16 / 45 back-left | 0.01 % | 0.00 % | 0.09 % |  |
| walk t=0.16 / 29 front | 0.02 % | 0.04 % | 0.89 % |  |
| walk t=0.16 / 29 right | 0.01 % | 0.34 % | 0.82 % |  |
| walk t=0.16 / 61 left | 0.02 % | 0.00 % | 0.04 % |  |
| walk t=0.16 / 61 back | 0.00 % | 0.00 % | 0.00 % |  |
| walk t=0.48 / 45 front | 0.01 % | 0.24 % | 1.26 % |  |
| walk t=0.48 / 45 back-right | 0.00 % | 0.00 % | 0.11 % |  |
| walk t=0.48 / 45 back-left | 0.03 % | 0.01 % | 0.23 % |  |
| walk t=0.48 / 29 front | 0.01 % | 0.40 % | 1.58 % |  |
| walk t=0.48 / 29 right | 0.02 % | 0.00 % | 0.12 % |  |
| walk t=0.48 / 61 left | 0.00 % | 0.00 % | 0.04 % |  |
| walk t=0.48 / 61 back | 0.00 % | 0.00 % | 0.00 % |  |
| walk t=0.80 / 45 front | 0.00 % | 0.29 % | 1.58 % |  |
| walk t=0.80 / 45 back-right | 0.00 % | 0.00 % | 0.09 % |  |
| walk t=0.80 / 45 back-left | 0.02 % | 0.00 % | 0.04 % |  |
| walk t=0.80 / 29 front | 0.00 % | 0.44 % | 1.73 % |  |
| walk t=0.80 / 29 right | 0.02 % | 0.22 % | 0.94 % |  |
| walk t=0.80 / 61 left | 0.01 % | 0.00 % | 0.09 % |  |
| walk t=0.80 / 61 back | 0.00 % | 0.00 % | 0.07 % |  |

