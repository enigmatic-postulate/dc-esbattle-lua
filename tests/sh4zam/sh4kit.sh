#!/usr/bin/env bash
# sh4kit.sh - runs the game's SH4 code under qemu: SH4ZAM's real SH4 back-end (FTRV, FSCA, FSRRA,
# MOVCA.L...), compiled like the DreamSDK build, for equivalence checks and instruction counts.
# Test tooling only: nothing here is part of the game or its makefile. See README.md.
#
#   sh4kit.sh rt                          build the qemu runtime (newlib -> Linux syscalls)
#   sh4kit.sh lib                         build SH4ZAM's library for SH4 from $SH4ZAM_DIR
#   sh4kit.sh plugin                      build the qemu plugins (instruction counts, cache model)
#   sh4kit.sh bench PROJECT OUT.elf       game + tests/sh4zam/bench_sh4.cpp (no-op engine)
#   sh4kit.sh trace PROJECT OUT.elf       game + the host trace harness (platform/host)
#   sh4kit.sh run ELF [args...]           run an ELF under qemu
#   sh4kit.sh count ELF INPUTS OUT [RD]   per-frame instruction counts (shcount.c) of a bench ELF
#   sh4kit.sh cache ELF INPUTS OUT [RD]   per-frame operand-cache misses (shcache.c) of a bench ELF
#
# PROJECT is a tree with src/ (this project, or an older copy to compare with). Environment:
#   SH_ELF      prefix of an sh-elf GCC with C++ and newlib for -ml -m4-single   (default ~/shx)
#   QEMU_SH4    qemu-sh4 10.x with tests/sh4zam/qemu-10.*-sh4-fpu.patch, built with plugins
#   QEMU_SRC    that qemu's source tree (for the plugin header)
#   SH4ZAM_DIR  SH4ZAM checkout                                  (default: as the makefile finds it)
#   KOS_FLAGS   code generation flags           (default: the DreamSDK build's, read from its ELF)
#   HOT_OPT     extra flags for the 3D hot paths, as the makefile's HOT_OPT          (default -O3)
#   WORK        build directory                                                    (default /tmp/sh4kit)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SH_ELF="${SH_ELF:-$HOME/shx}"
X="$SH_ELF/bin/sh-elf"
QEMU_SH4="${QEMU_SH4:-qemu-sh4}"
WORK="${WORK:-/tmp/sh4kit}"
KOS_FLAGS="${KOS_FLAGS:--ml -m4-single -mfsrra -mfsca -matomic-model=soft-gusa -O2 -fno-PIE -fomit-frame-pointer -ffunction-sections -fdata-sections -ftls-model=local-exec}"
HOT_OPT="${HOT_OPT--O3}"
if [ -z "${SH4ZAM_DIR:-}" ]; then
    for d in "$ROOT/external/sh4zam" "$ROOT/../sh4zam/sh4zam"; do [ -f "$d/include/sh4zam/shz_sh4zam.hpp" ] && SH4ZAM_DIR="$d" && break; done
fi
SH4ZAM_DIR="$(cd "${SH4ZAM_DIR:?set SH4ZAM_DIR to an SH4ZAM checkout}" && pwd)"
mkdir -p "$WORK"

cxx() { "$X-g++" $KOS_FLAGS -DNDEBUG -I"$SH4ZAM_DIR/include" "$@"; }

link_elf() {   # OUT OBJ...
    local out="$1"; shift
    local gcclib; gcclib="$(dirname "$("$X-gcc" $KOS_FLAGS -print-libgcc-file-name)")"
    local cxxlib; cxxlib="$(dirname "$("$X-g++" $KOS_FLAGS -print-file-name=libstdc++.a)")"
    "$X-g++" $KOS_FLAGS -nostdlib -Wl,-e,start -Wl,-Ttext=0x400000 -Wl,-z,max-page-size=0x1000 \
        -Wl,--defsym,_stack=0x30000000 -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
        "$gcclib/crti.o" "$gcclib/crtbegin.o" "$@" "$WORK/newlib_linux.o" "$WORK/linux_sys.o" "$WORK/libsh4zam_sh4.a" \
        -L"$cxxlib" -lstdc++ -lsupc++ "$WORK/libc_linux.a" "$WORK/libm.a" "$WORK/libc_linux.a" -lgcc "$WORK/libc_linux.a" \
        "$gcclib/crtend.o" "$gcclib/crtn.o" -o "$out"
}

build_game() {   # PROJECT OUT EXTRA_SOURCE...
    local proj="$1" out="$2"; shift 2
    local o; o="$(mktemp -d)"
    for f in "$proj"/src/*.cpp; do
        local hot=""
        case "$(basename "$f")" in render3d.cpp|field3d.cpp|dmf.cpp|anim.cpp|arena.cpp|battle3d_draw.cpp) hot="$HOT_OPT";; esac
        cxx $hot -c "$f" -o "$o/$(basename "$f" .cpp).o" &
    done
    for f in "$@"; do cxx -I"$proj/src" -c "$f" -o "$o/x_$(basename "$f" .cpp).o" & done
    wait
    link_elf "$out" "$o"/*.o
    rm -rf "$o"
}

addr() { printf "0x%s" "$("$X-nm" "$1" | awk -v s="$2" '$3==s {print $1}')"; }

case "${1:-}" in
rt)
    libc="$("$X-gcc" $KOS_FLAGS -print-file-name=libc.a)"
    cp "$libc" "$WORK/libc_linux.a"
    "$X-ar" d "$WORK/libc_linux.a" lib_a-syscalls.o        # newlib's simulator syscalls
    cp "$("$X-gcc" $KOS_FLAGS -print-file-name=libm.a)" "$WORK/libm.a"
    "$X-gcc" $KOS_FLAGS -c "$HERE/qemu-rt/newlib_linux.c" -o "$WORK/newlib_linux.o"
    "$X-gcc" $KOS_FLAGS -c "$HERE/qemu-rt/linux_sys.S" -o "$WORK/linux_sys.o"
    ;;
lib)
    # SH4ZAM's own source list for the Dreamcast (its CMakeLists.txt, PLATFORM_DREAMCAST)
    o="$(mktemp -d)"
    for f in shz_version.c sh4/shz_complex_sh4.c sh4/shz_matrix_sh4.c sh4/shz_xmtrx_sh4.s sh4/shz_mem_sh4.s \
             sw/shz_complex_sw.c sw/shz_matrix_sw.c sw/shz_xmtrx_sw.c; do
        "$X-gcc" -std=gnu11 $KOS_FLAGS -DNDEBUG -I"$SH4ZAM_DIR/include" -c "$SH4ZAM_DIR/source/$f" -o "$o/$(basename "$f").o"
    done
    rm -f "$WORK/libsh4zam_sh4.a"; "$X-ar" rcs "$WORK/libsh4zam_sh4.a" "$o"/*.o; rm -rf "$o"
    ;;
plugin)
    for p in shcount shcache; do
        cc -shared -fPIC -O2 -Wall -I"${QEMU_SRC:?set QEMU_SRC}/include/qemu" $(pkg-config --cflags glib-2.0) \
            "$HERE/$p.c" -o "$WORK/lib$p.so"
    done
    ;;
bench) build_game "$2" "$3" "$HERE/bench_sh4.cpp" ;;
trace) build_game "$2" "$3" "$2/platform/host/host_engine.cpp" "$2/platform/host/trace_main.cpp" "$2/platform/host/battle_bot.cpp" "$2/platform/host/story.cpp" ;;
run)   shift; exec "$QEMU_SH4" "$@" ;;
count|cache)
    elf="$2"; in="$3"; out="$4"; rd="${5:-$ROOT/romdisk}"
    plug="$WORK/libshcount.so"; [ "$1" = cache ] && plug="$WORK/libshcache.so"
    "$QEMU_SH4" -plugin "$plug,tick=$(addr "$elf" _bench_mark_tick),draw=$(addr "$elf" _bench_mark_draw),end=$(addr "$elf" _bench_mark_end),out=$out" \
        "$elf" "$in" "$rd"
    ;;
*) sed -n '2,22p' "$0"; exit 2 ;;
esac
