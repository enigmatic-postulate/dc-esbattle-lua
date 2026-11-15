#
# C++ battle port (KOS + PVR) - no Lua.
#
TARGET = battle.elf

SRC_OBJS = src/assets.o src/battle.o src/chars.o src/combat.o src/dmf.o src/encounters.o src/field.o \
           src/field3d.o src/font.o src/fx.o src/game.o src/hpbar.o src/icons.o src/input.o \
           src/input_probe.o src/presentation.o src/render3d.o src/skills.o src/status.o \
           src/targeting.o src/turn_system.o src/ui.o
OBJS = platform/kos/main.o $(SRC_OBJS) romdisk.o

# romdisk/: PNGs, maps/ and models/*.dmf (rebuild models with tools/dmf/build_models.sh)
KOS_ROMDISK_DIR = romdisk

KOS_CFLAGS   += -I$(KOS_PORTS)/include
KOS_LIBS     += -lpng -lz

# ---- SH4ZAM: the hardware math layer (MIT, https://github.com/gyrovorbis/sh4zam) -------------
# The renderer's matrix, vector, quaternion and trig math calls SH4ZAM (src/math3d.h), which is
# linked as a library and never copied into this tree (SH4ZAM's AGENTS.md). Two sources:
#  1. Default: the KallistiOS port. DreamSDK: DreamSDK Manager > "KallistiOS Ports" > SH4ZAM >
#     Install; a plain KOS tree: cd $KOS_PORTS/sh4zam && make install. Headers then live in
#     $(KOS_PORTS)/include/sh4zam and the library in $(KOS_PORTS)/lib (already on the paths).
#  2. A full upstream checkout (e.g. to pin a version as a git submodule):
#        git submodule add https://github.com/gyrovorbis/sh4zam.git external/sh4zam
#        make SH4ZAM_DIR=external/sh4zam
#     It is built out of tree with SH4ZAM's own CMake project (kos-cmake) into build/sh4zam-kos.
# Version 0.10.0 or newer is required (checked at compile time in src/math3d.h); the boot log
# prints the linked version. Release builds define NDEBUG, as SH4ZAM recommends (its debug
# asserts check alignment in the hot loops); make SHZ_DEBUG=1 keeps them.
ifdef SH4ZAM_DIR
SH4ZAM_KOS_BUILD = build/sh4zam-kos
SH4ZAM_LIB       = $(SH4ZAM_KOS_BUILD)/libsh4zam.a
KOS_CFLAGS      := -I$(SH4ZAM_DIR)/include $(KOS_CFLAGS)
else
SH4ZAM_LIB       =
KOS_LIBS        += -lsh4zam
endif
ifndef SHZ_DEBUG
KOS_CFLAGS   += -DNDEBUG
endif

# The 3D hot paths at -O3, the rest at the KOS default (SH4ZAM's optimisation guide). Measured on
# the SH4 code: about 5% fewer instructions per field frame. make HOT_OPT= builds them at -O2.
HOT_OPT ?= -O3
src/render3d.o src/field3d.o src/dmf.o: CXXFLAGS += $(HOT_OPT)

# Header dependencies: every .o gets a .d listing the headers it includes, so editing a header
# (e.g. src/field.h) rebuilds every object that uses it. Without this a plain `make` after an
# update kept objects compiled against the old struct layouts (e.g. game.o vs FieldState).
KOS_CFLAGS   += -MMD -MP

# A/B triage: make PVR_DEFAULTS=1 uses pvr_init_defaults() (512 KiB TA vertex buffers)
ifdef PVR_DEFAULTS
KOS_CFLAGS += -DPVR_DEFAULTS=1
endif

# 3D triage: make R3D_SAFE=1 submits 3D strips one vertex per pvr_prim() (see platform/kos/main.cpp)
ifdef R3D_SAFE
KOS_CFLAGS += -DR3D_SAFE=1
endif

# Crash triage: make DIAG=1 [DIAG_SKIP=game|update|draw]   (see platform/kos/main.cpp)
ifdef DIAG
KOS_CFLAGS += -DBATTLE_DIAG=1 -fno-omit-frame-pointer
ifeq ($(DIAG_SKIP),game)
KOS_CFLAGS += -DDIAG_SKIP_GAME
endif
ifeq ($(DIAG_SKIP),update)
KOS_CFLAGS += -DDIAG_SKIP_UPDATE
endif
ifeq ($(DIAG_SKIP),draw)
KOS_CFLAGS += -DDIAG_SKIP_DRAW
endif
endif

all: rm-elf sh4zam-check $(TARGET)

ifneq ($(KOS_BASE),)
include $(KOS_BASE)/Makefile.rules
endif

# Fails early, with instructions, when SH4ZAM is not installed (instead of a missing-header error).
.PHONY: sh4zam-check
sh4zam-check:
ifdef SH4ZAM_DIR
	@test -f "$(SH4ZAM_DIR)/include/sh4zam/shz_sh4zam.hpp" || { echo "SH4ZAM_DIR=$(SH4ZAM_DIR) is not an SH4ZAM checkout (no include/sh4zam/shz_sh4zam.hpp)"; exit 1; }
else
	@test -f "$(KOS_PORTS)/include/sh4zam/shz_sh4zam.hpp" || { \
	  echo "SH4ZAM is not installed in $(KOS_PORTS)."; \
	  echo "  DreamSDK: DreamSDK Manager > KallistiOS Ports > SH4ZAM > Install"; \
	  echo "  KOS:      cd \$$KOS_PORTS/sh4zam && make install"; \
	  echo "  or build against a checkout: make SH4ZAM_DIR=<path to sh4zam>"; exit 1; }
endif

$(OBJS): | sh4zam-check

ifdef SH4ZAM_DIR
# SH4ZAM's own CMake project, configured for KOS, library target only.
.PHONY: $(SH4ZAM_LIB)
$(SH4ZAM_LIB): | sh4zam-check
	kos-cmake -S $(SH4ZAM_DIR) -B $(SH4ZAM_KOS_BUILD) -DCMAKE_BUILD_TYPE=Release -DSHZ_ENABLE_TESTS=OFF
	$(MAKE) -C $(SH4ZAM_KOS_BUILD) sh4zam
endif

clean: rm-elf
	-rm -f $(OBJS) $(OBJS:.o=.d)

-include $(SRC_OBJS:.o=.d) platform/kos/main.d

rm-elf:
	-rm -f $(TARGET) romdisk.*

$(TARGET): $(OBJS) $(SH4ZAM_LIB)
	kos-c++ -o $(TARGET) $(OBJS) $(SH4ZAM_LIB) $(KOS_LIBS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS) romdisk.img
	$(KOS_STRIP) $(TARGET)

# ---- host equivalence build (no KOS needed) ----
# Uses SH4ZAM's portable software back-end, built from an upstream checkout with SH4ZAM's own CMake
# project (the KOS port ships no host sources). Checkout: SH4ZAM_DIR, else external/sh4zam, else
# ../sh4zam/sh4zam (where the library sits next to this project in the delivered tree).
SH4ZAM_SRC      := $(or $(SH4ZAM_DIR),$(wildcard external/sh4zam),$(wildcard ../sh4zam/sh4zam))
HOST_SHZ_BUILD   = build/sh4zam-host
HOST_SHZ_LIB     = $(HOST_SHZ_BUILD)/libsh4zam.a
HOST_SHZ_FLAGS   = -isystem $(SH4ZAM_SRC)/include -DSHZ_BACKEND=SHZ_SW -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT
HOST_CXX         = g++ -std=gnu++17

$(HOST_SHZ_LIB):
	@test -n "$(SH4ZAM_SRC)" || { echo "host build needs an SH4ZAM checkout: make host SH4ZAM_DIR=<path>"; exit 1; }
	cmake -S $(SH4ZAM_SRC) -B $(HOST_SHZ_BUILD) -DCMAKE_BUILD_TYPE=Release -DSHZ_BACKEND=SW -DSHZ_TLS_MODEL=IMPLICIT -DSHZ_ENABLE_TESTS=OFF
	cmake --build $(HOST_SHZ_BUILD) --target sh4zam

.PHONY: host
host: $(HOST_SHZ_LIB)
	mkdir -p build
	$(HOST_CXX) -O2 -Wall -Wextra $(HOST_SHZ_FLAGS) -DBATTLE_REAL_DOUBLE src/*.cpp platform/host/host_engine.cpp platform/host/trace_main.cpp $(HOST_SHZ_LIB) -o build/battle_trace_f64
	$(HOST_CXX) -O2 -Wall -Wextra $(HOST_SHZ_FLAGS) src/*.cpp platform/host/host_engine.cpp platform/host/trace_main.cpp $(HOST_SHZ_LIB) -o build/battle_trace_f32
	$(HOST_CXX) -O2 $(HOST_SHZ_FLAGS) src/*.cpp platform/bench/bench_main.cpp $(HOST_SHZ_LIB) -o build/bench_f32

# Prints the host compile flags for SH4ZAM (used by tests/run_tests.sh).
.PHONY: print-host-shz-flags
print-host-shz-flags:
	@echo $(HOST_SHZ_FLAGS)
