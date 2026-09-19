# KallistiOS ##version##
#
# First-class SH4ZAM integration example
# Copyright (C) 2026 Joseph Black
#

TARGET = sh4zam-integration
OBJS = $(TARGET).o

all: rm-elf $(TARGET).elf

include $(KOS_BASE)/Makefile.rules

KOS_CFLAGS += -I$(KOS_BASE)/utils/animation-test
KOS_CFLAGS += -I$(KOS_BASE)/utils/pvr-chunk-cache-test
$(OBJS): $(KOS_BASE)/utils/animation-test/matrix-fixtures.h
$(OBJS): $(KOS_BASE)/utils/pvr-chunk-cache-test/toon-draw-fixtures.h

clean: rm-elf
	-rm -f $(OBJS)

rm-elf:
	-rm -f $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(OBJS)
	kos-cc -o $(TARGET).elf $(OBJS)

run: $(TARGET).elf
	$(KOS_LOADER) $(TARGET).elf

dist: $(TARGET).elf
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET).elf
	$(KOS_OBJCOPY) -R .stack -O binary $(TARGET).elf $(TARGET).bin
