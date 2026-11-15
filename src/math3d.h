// math3d.h - float math for the 3D field renderer (presentation only; game rules stay integer).
// Conventions: right-handed, +Y up, models face +Z. Mat34 is a 3x4 row-major affine matrix
// (rows = output x, y, z); points transform as  p' = M * [p, 1].
//
// Hardware math layer: every operation here forwards to SH4ZAM (MIT, github.com/gyrovorbis/sh4zam),
// linked as a library (the KallistiOS port, or an upstream checkout - see hwmath.h). This file
// follows SH4ZAM's "interop" pattern: the game keeps its own types and names, SH4ZAM does the work
// through zero-cost layout conversions. On the Dreamcast (SH4 back-end) that means
//   * FTRV  - XMTRX matrix * vector, for matrix products and batched point transforms;
//   * FSCA  - sine + cosine in one instruction (shz_sincosf);
//   * FSRRA - 1/sqrt(x), used for normalisation, sqrt and the 1/W perspective divide;
// the host harness compiles the same calls against SH4ZAM's portable software back-end.
//
// Layouts:
//   * Vec3  : 3 floats, converted to and from shz_vec3_t component by component (free: the
//             values live in FP registers). Never by pointer cast: viewing a Vec3 as a shz::vec3
//             is type punning, which GCC's strict-aliasing optimisation (on at -O2) is allowed to
//             break - and did, on the SH4 build, where a store to a Vec3 was moved past the read.
//   * Quat  : shz::quat, stored W, X, Y, Z (SH4ZAM order). Build it with quat_xyzw() from data
//             stored X, Y, Z, W (the .dmf files).
//   * Mat34 : 12 floats, row-major. That is exactly the memory SH4ZAM's *_transpose_3x4 XMTRX
//             routines read and write (rows 0-2 of XMTRX; row 3 is 0 0 0 1), so a Mat34 moves
//             into the SH4's matrix register bank and back without any reshuffling. The SH4ZAM
//             view of those floats is a union member (Mat34::rows), not a pointer cast, so the
//             compiler's alias analysis sees every XMTRX load/store touching the matrix.
//
// XMTRX rule for this codebase: XMTRX is scratch state. Code that transforms a batch loads it
// first (xmtrx_load / xmtrx_apply) and must not call anything that may reload it (mat_mul,
// mat_look_at, camera_look, ...) until the batch is done. One-off helpers (mat_point, mat_dir,
// mat_dir_t, mat_trs_y, quat_nlerp) never touch XMTRX.
#pragma once
#include <math.h>
#include "hwmath.h"

namespace r3d {

struct Vec3 { float x, y, z; };
typedef shz::quat Quat;
// m[row][col]; `rows` is the same 12 floats typed for SH4ZAM's *_transpose_3x4 routines (which
// treat them as three rows - this layout). Only ever passed to those routines.
struct Mat34 { union { float m[3][4]; shz_mat3x4_t rows; }; };

static_assert(sizeof(Mat34) == 12 * sizeof(float), "Mat34 must stay 12 floats");

// ---------------------------------------------------------------------------- interop
inline Vec3 v3(float x, float y, float z) { Vec3 v = { x, y, z }; return v; }
inline shz_vec3_t sv(Vec3 v) { return shz_vec3_init(v.x, v.y, v.z); }         // Vec3 -> SH4ZAM
inline Vec3 rv(shz_vec3_t v) { return v3(v.x, v.y, v.z); }                       // SH4ZAM -> Vec3

// ---------------------------------------------------------------------------- vectors
inline Vec3 operator+(Vec3 a, Vec3 b) { return rv(shz_vec3_add(sv(a), sv(b))); }
inline Vec3 operator-(Vec3 a, Vec3 b) { return rv(shz_vec3_sub(sv(a), sv(b))); }
inline Vec3 operator*(Vec3 a, float s) { return rv(shz_vec3_scale(sv(a), s)); }
inline float dot(Vec3 a, Vec3 b) { return shz_vec3_dot(sv(a), sv(b)); }
inline Vec3 cross(Vec3 a, Vec3 b) { return rv(shz_vec3_cross(sv(a), sv(b))); }
// Unit vector (FSRRA); the zero vector stays zero.
inline Vec3 normalize(Vec3 a) { return rv(shz_vec3_normalize_safe(sv(a))); }
inline float length(Vec3 a) { return shz_vec3_magnitude(sv(a)); }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return rv(shz_vec3_lerp(sv(a), sv(b), t)); }

// ---------------------------------------------------------------------------- XMTRX (batched)
// XMTRX = a (as a 4x4 affine matrix).
inline void xmtrx_load(const Mat34& a) { shz_xmtrx_load_transpose_3x4(&a.rows); }
// XMTRX = XMTRX * b.
inline void xmtrx_apply(const Mat34& b) { shz_xmtrx_apply_transpose_3x4(&b.rows); }
// Top three rows of XMTRX -> out.
inline void xmtrx_store(Mat34* out) { shz_xmtrx_store_transpose_3x4(&out->rows); }
// XMTRX * [p, 1]: one FTRV.
inline Vec3 xmtrx_point(Vec3 p) { return rv(shz_xmtrx_transform_point3(sv(p))); }
inline Vec3 xmtrx_point(float x, float y, float z) { return rv(shz_xmtrx_transform_point3(shz_vec3_init(x, y, z))); }

// ---------------------------------------------------------------------------- matrices
inline Mat34 mat_identity() {
    Mat34 r = { { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } } };
    return r;
}

// a * b, through XMTRX (clobbers it).
inline Mat34 mat_mul(const Mat34& a, const Mat34& b) {
    Mat34 r;
    xmtrx_load(a);
    xmtrx_apply(b);
    xmtrx_store(&r);
    return r;
}

// One-off transforms: SH4ZAM dot products on the rows (XMTRX untouched). For many points against
// one matrix, use xmtrx_load() + xmtrx_point() instead.
inline Vec3 mat_point(const Mat34& a, Vec3 p) {
    return v3(shz_dot8f(a.m[0][0], a.m[0][1], a.m[0][2], a.m[0][3], p.x, p.y, p.z, 1.0f),
              shz_dot8f(a.m[1][0], a.m[1][1], a.m[1][2], a.m[1][3], p.x, p.y, p.z, 1.0f),
              shz_dot8f(a.m[2][0], a.m[2][1], a.m[2][2], a.m[2][3], p.x, p.y, p.z, 1.0f));
}

inline Vec3 mat_dir(const Mat34& a, Vec3 p) {
    return v3(shz_dot6f(a.m[0][0], a.m[0][1], a.m[0][2], p.x, p.y, p.z),
              shz_dot6f(a.m[1][0], a.m[1][1], a.m[1][2], p.x, p.y, p.z),
              shz_dot6f(a.m[2][0], a.m[2][1], a.m[2][2], p.x, p.y, p.z));
}

// Transpose of the 3x3 part applied to a direction (inverse rotation for rigid matrices).
inline Vec3 mat_dir_t(const Mat34& a, Vec3 p) {
    return v3(shz_dot6f(a.m[0][0], a.m[1][0], a.m[2][0], p.x, p.y, p.z),
              shz_dot6f(a.m[0][1], a.m[1][1], a.m[2][1], p.x, p.y, p.z),
              shz_dot6f(a.m[0][2], a.m[1][2], a.m[2][2], p.x, p.y, p.z));
}

// [R(q) | t], through XMTRX (clobbers it): the SH4 back-end builds R(q) straight in the register bank.
inline Mat34 mat_from_qt(const Quat& q, Vec3 t) {
    Mat34 r;
    shz_xmtrx_init_rotation_quat(q);
    shz_xmtrx_set_translation(t.x, t.y, t.z);
    xmtrx_store(&r);
    return r;
}

// Translation * RotationY(yaw) * uniform/non-uniform scale. yaw = 0 faces +Z; yaw > 0 turns toward +X.
// One FSCA for both sine and cosine.
inline Mat34 mat_trs_y(Vec3 t, float yaw, float sx, float sy, float sz) {
    const shz_sincos_t sc = shz_sincosf(yaw);
    const float c = sc.cos, s = sc.sin;
    Mat34 r = { { { { c * sx, 0, s * sz, t.x }, { 0, sy, 0, t.y }, { -s * sx, 0, c * sz, t.z } } } };
    return r;
}

// View matrix: camera at `eye` looking at `target` (camera looks down its -Z); gluLookAt, built in
// XMTRX by SH4ZAM (clobbers it).
inline Mat34 mat_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Mat34 m;
    shz_xmtrx_init_lookat(sv(eye), sv(target), sv(up));
    xmtrx_store(&m);
    return m;
}

// ---------------------------------------------------------------------------- quaternions
// From components stored X, Y, Z, W (the .dmf files).
inline Quat quat_xyzw(float x, float y, float z, float w) { return Quat(w, x, y, z); }

// Shortest-path normalised lerp (sign flip + FSRRA normalise).
SHZ_FORCE_INLINE Quat quat_nlerp(const Quat& a, const Quat& b, float t) { return shz_quat_nlerp(a, b, t); }

// ---------------------------------------------------------------------------- scalars
// Shortest signed angle difference b - a, wrapped to (-pi, pi].
inline float angle_diff(float a, float b) {
    float d = shz_fmodf(b - a, SHZ_F_PI * 2.0f);
    if (d > SHZ_F_PI) d -= SHZ_F_PI * 2.0f;
    if (d <= -SHZ_F_PI) d += SHZ_F_PI * 2.0f;
    return d;
}

} // namespace r3d
