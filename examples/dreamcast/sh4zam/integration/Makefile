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
$(OBJS): release-fixtures.h

# Diagnostic lane: validate the optional fast-math inline implementation from
# a strict caller. It is separate from the default KOS integration gate.
fast-trig-probe.elf: fast-trig-probe.o fast-trig-call.o
	kos-cc -o $@ $^

fast-trig-call.o: fast-trig-call.c
	kos-cc -ffast-math -c $< -o $@

fast-trig-probe.o: fast-trig-probe.c
	kos-cc -fno-fast-math -c $< -o $@

clean: rm-elf
	-rm -f $(OBJS)
	-rm -f fast-trig-probe.elf fast-trig-probe.o fast-trig-call.o

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
