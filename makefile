#
# PNG Sprite Demo (KOS + PVR)
#

TARGET = sprite_demo.elf
OBJS   = main.o romdisk.o

# This tells Makefile.rules to build romdisk.img + romdisk.o automatically
KOS_ROMDISK_DIR = romdisk

# If your png headers are in kos-ports include path automatically, this is optional.
# If not, uncomment and adjust:
# KOS_CFLAGS += -I$(KOS_PORTS)/include

# Link against libpng + zlib (kos-ports) + lua
KOS_CFLAGS += -I$(KOS_PORTS)/include
KOS_LIBS   += -llua -lpng -lz

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

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
