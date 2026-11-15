// hwmath.h - the project's one include point for SH4ZAM, the SH4 hardware math library
// (MIT, https://github.com/gyrovorbis/sh4zam). It is linked as a library - the KallistiOS port,
// or an upstream checkout (see the makefile) - and never copied into this tree.
//
// Fails early, with instructions, when the headers are missing or older than this code needs:
// 0.10.0 (the XMTRX 3x4 transpose family came in 0.9.0, the GL-style transform names in 0.10.0).
#pragma once

#if defined(__has_include)
#if !__has_include(<sh4zam/shz_sh4zam.hpp>)
#error "SH4ZAM not found. DreamSDK: DreamSDK Manager > KallistiOS Ports > SH4ZAM > Install. KOS: cd $KOS_PORTS/sh4zam && make install. Or build against a checkout: make SH4ZAM_DIR=<path>"
#endif
#endif

#include <sh4zam/shz_version.h>
#if !defined(SHZ_VERSION_MAJOR) || (SHZ_VERSION_MAJOR == 0 && SHZ_VERSION_MINOR < 10)
#error "SH4ZAM 0.10.0 or newer is required: update the KallistiOS port (DreamSDK: KallistiOS Ports tab; KOS: cd $KOS_PORTS/sh4zam && make reinstall), or build against a checkout with make SH4ZAM_DIR=<path>"
#endif

// Library headers: keep their style warnings (e.g. -Wshadow on constructor parameters) out of
// this project's builds, as with any system header.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <sh4zam/shz_sh4zam.hpp>
#pragma GCC diagnostic pop
