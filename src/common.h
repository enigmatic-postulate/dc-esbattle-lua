// common.h - shared primitives for the C++ battle port.
#pragma once
#include <stdint.h>
#include <math.h>

namespace bt {

// Scalar type for positions/timers. Float on hardware (SH4 FPU is single-precision
// fast-path). The host equivalence harness can build with -DBATTLE_REAL_DOUBLE to
// match the Lua reference (lua_Number = double) bit-for-bit and isolate logic
// differences from precision differences.
#ifdef BATTLE_REAL_DOUBLE
typedef double real;
#else
typedef float real;
#endif

// Fixed timestep (slice task A5). All gameplay timers count ticks; TICK_DT is only used
// for continuous motion/cosmetics. Durations below were calibrated so the tick version
// reproduces the previous float32 build running at exactly 60 Hz frame-for-frame.
const int  TICKS_PER_SEC = 60;
const real TICK_DT = (real)1 / (real)60;

template <class T> inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> inline T maxv(T a, T b) { return a > b ? a : b; }
template <class T> inline T minv(T a, T b) { return a < b ? a : b; }

// Fixed-capacity vector: no heap, no per-frame allocation.
// swap_remove() reproduces the Lua idiom `t[i] = t[#t]; t[#t] = nil` exactly,
// which matters because iteration/draw order is observable.
template <class T, int N>
struct FixedVec {
    T   data[N];
    int n = 0;

    int  size() const  { return n; }
    bool empty() const { return n == 0; }
    bool full() const  { return n >= N; }
    void clear()       { n = 0; }
    T&       operator[](int i)       { return data[i]; }
    const T& operator[](int i) const { return data[i]; }
    T*       begin()       { return data; }
    T*       end()         { return data + n; }
    const T* begin() const { return data; }
    const T* end()   const { return data + n; }

    bool push(const T& v) {           // returns false (drops) when full
        if (n >= N) return false;
        data[n++] = v;
        return true;
    }
    void swap_remove(int i) { data[i] = data[n - 1]; --n; }
};

} // namespace bt
