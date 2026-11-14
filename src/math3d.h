// math3d.h - minimal float math for the 3D field renderer (presentation only; game rules stay
// integer). Conventions: right-handed, +Y up, models face +Z. Mat34 is a 3x4 row-major affine
// matrix (rows = output x, y, z); points transform as  p' = M * [p, 1].
#pragma once
#include <math.h>

namespace r3d {

struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };
struct Mat34 { float m[3][4]; };

inline Vec3 v3(float x, float y, float z) { Vec3 v = { x, y, z }; return v; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline Vec3 normalize(Vec3 a) { float l = sqrtf(dot(a, a)); return l > 0.0f ? a * (1.0f / l) : a; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

inline Mat34 mat_identity() {
    Mat34 r = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
    return r;
}

inline Mat34 mat_mul(const Mat34& a, const Mat34& b) {
    Mat34 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        r.m[i][3] += a.m[i][3];
    }
    return r;
}

inline Vec3 mat_point(const Mat34& a, Vec3 p) {
    return v3(a.m[0][0] * p.x + a.m[0][1] * p.y + a.m[0][2] * p.z + a.m[0][3],
              a.m[1][0] * p.x + a.m[1][1] * p.y + a.m[1][2] * p.z + a.m[1][3],
              a.m[2][0] * p.x + a.m[2][1] * p.y + a.m[2][2] * p.z + a.m[2][3]);
}

inline Vec3 mat_dir(const Mat34& a, Vec3 p) {
    return v3(a.m[0][0] * p.x + a.m[0][1] * p.y + a.m[0][2] * p.z,
              a.m[1][0] * p.x + a.m[1][1] * p.y + a.m[1][2] * p.z,
              a.m[2][0] * p.x + a.m[2][1] * p.y + a.m[2][2] * p.z);
}

// Transpose of the 3x3 part applied to a direction (inverse rotation for rigid matrices).
inline Vec3 mat_dir_t(const Mat34& a, Vec3 p) {
    return v3(a.m[0][0] * p.x + a.m[1][0] * p.y + a.m[2][0] * p.z,
              a.m[0][1] * p.x + a.m[1][1] * p.y + a.m[2][1] * p.z,
              a.m[0][2] * p.x + a.m[1][2] * p.y + a.m[2][2] * p.z);
}

inline Mat34 mat_from_qt(Quat q, Vec3 t) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat34 r = { { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), t.x },
                  { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), t.y },
                  { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), t.z } } };
    return r;
}

// Translation * RotationY(yaw) * uniform/non-uniform scale. yaw = 0 faces +Z; yaw > 0 turns toward +X.
inline Mat34 mat_trs_y(Vec3 t, float yaw, float sx, float sy, float sz) {
    const float c = cosf(yaw), s = sinf(yaw);
    Mat34 r = { { { c * sx, 0, s * sz, t.x }, { 0, sy, 0, t.y }, { -s * sx, 0, c * sz, t.z } } };
    return r;
}

// View matrix: camera at `eye` looking at `target` (camera looks down its -Z).
inline Mat34 mat_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = normalize(target - eye);
    Vec3 r = normalize(cross(f, up));
    Vec3 u = cross(r, f);
    Mat34 m = { { { r.x, r.y, r.z, -dot(r, eye) },
                  { u.x, u.y, u.z, -dot(u, eye) },
                  { -f.x, -f.y, -f.z, dot(f, eye) } } };
    return m;
}

inline Quat quat_nlerp(Quat a, Quat b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
    Quat q = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    float inv = l > 0.0f ? 1.0f / l : 0.0f;
    q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    return q;
}

// Shortest signed angle difference b - a, wrapped to (-pi, pi].
inline float angle_diff(float a, float b) {
    float d = fmodf(b - a, 6.28318531f);
    if (d > 3.14159265f) d -= 6.28318531f;
    if (d <= -3.14159265f) d += 6.28318531f;
    return d;
}

} // namespace r3d
