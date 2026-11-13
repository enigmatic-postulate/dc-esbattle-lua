#
# C++ battle port (KOS + PVR) - no Lua.
#
TARGET = battle.elf

SRC_OBJS = src/assets.o src/battle.o src/combat.o src/encounters.o src/fx.o src/icons.o \
           src/hpbar.o src/input_probe.o src/presentation.o src/skills.o \
           src/status.o src/targeting.o src/turn_system.o src/ui.o
OBJS = platform/kos/main.o $(SRC_OBJS) romdisk.o

# romdisk/ now only needs the PNGs (no .lua files)
KOS_ROMDISK_DIR = romdisk

KOS_CFLAGS   += -I$(KOS_PORTS)/include
KOS_LIBS     += -lpng -lz

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
	-rm -f $(OBJS)

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
