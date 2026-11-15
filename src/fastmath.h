// fastmath.h - cosmetic math on `real` values for the 2D battle presentation (sprite wobble, bob,
// lunge direction, alpha rounding). Game rules never use these.
//
// Float builds (the Dreamcast, and the host float harness) go through SH4ZAM: FSCA for sine,
// FSRRA for square roots on the SH4 back-end (see math3d.h). The host equivalence build
// (-DBATTLE_REAL_DOUBLE, real = double, compared against the Lua reference) keeps libm's double
// precision, since SH4ZAM is single precision only.
#pragma once
#include "common.h"
#include "hwmath.h"

namespace bt {

#ifdef BATTLE_REAL_DOUBLE
inline real rsin(real x)   { return sin(x); }
inline real rsqrt(real x)  { return sqrt(x); }
inline real rfloor(real x) { return floor(x); }
#else
inline real rsin(real x)   { return shz_sinf(x); }
inline real rsqrt(real x)  { return shz_sqrtf(x); }
inline real rfloor(real x) { return shz_floorf(x); }
#endif

} // namespace bt
