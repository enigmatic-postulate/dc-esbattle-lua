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

all: rm-elf $(TARGET)

ifneq ($(KOS_BASE),)
include $(KOS_BASE)/Makefile.rules
endif

clean: rm-elf
	-rm -f $(OBJS) $(OBJS:.o=.d)

-include $(SRC_OBJS:.o=.d) platform/kos/main.d

rm-elf:
	-rm -f $(TARGET) romdisk.*

$(TARGET): $(OBJS)
	kos-c++ -o $(TARGET) $(OBJS) $(KOS_LIBS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS) romdisk.img
	$(KOS_STRIP) $(TARGET)

# ---- host equivalence build (no KOS needed) ----
.PHONY: host
host:
	mkdir -p build
	g++ -std=c++11 -O2 -Wall -Wextra -DBATTLE_REAL_DOUBLE src/*.cpp platform/host/host_engine.cpp platform/host/trace_main.cpp -o build/battle_trace_f64
	g++ -std=c++11 -O2 -Wall -Wextra src/*.cpp platform/host/host_engine.cpp platform/host/trace_main.cpp -o build/battle_trace_f32
	g++ -std=c++11 -O2 src/*.cpp platform/bench/bench_main.cpp -o build/bench_f32
