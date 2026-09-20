# KallistiOS ##version##
#
# First-class SH4ZAM integration example
# Copyright (C) 2026 Joseph Black
#

TARGET = sh4zam-integration
OBJS = $(TARGET).o

all: rm-elf $(TARGET).elf u16-trig-test.elf fast-trig-probe.elf

include $(KOS_BASE)/Makefile.rules

KOS_CFLAGS += -I$(KOS_BASE)/utils/animation-test
KOS_CFLAGS += -I$(KOS_BASE)/utils/pvr-chunk-cache-test
$(OBJS): $(KOS_BASE)/utils/animation-test/matrix-fixtures.h
$(OBJS): $(KOS_BASE)/utils/pvr-chunk-cache-test/toon-draw-fixtures.h
$(OBJS): release-fixtures.h

# Direct API regression: PR #70 fixes the failure in the official 0.8.1 pin.
# Keep the caller separate from its strict validator and disable LTO.
fast-trig-probe.elf: fast-trig-probe.o fast-trig-call.o
	kos-cc -fno-fast-math -fno-lto -o $@ $^

fast-trig-call.o: fast-trig-call.c
	kos-cc -ffast-math -fno-lto -c $< -o $@

fast-trig-probe.o: fast-trig-probe.c
	kos-cc -fno-fast-math -fno-lto -c $< -o $@

# KOS helper regression: keep the validator strict and calls in separate TUs
# without LTO so runtime arguments cannot become constant-folded test cases.
U16_OBJS = u16-trig-test.o u16-trig-strict.o u16-trig-fast.o \
           u16-trig-ofast.o u16-trig-cpp.o
$(U16_OBJS): $(KOS_BASE)/addons/include/kos/sh4zam.h

SHZ_TRIG_HEADERS = $(KOS_BASE)/addons/include/sh4zam/shz_trig.h \
    $(KOS_BASE)/addons/include/sh4zam/inline/shz_trig.inl.h \
    $(KOS_BASE)/addons/include/sh4zam/inline/sh4/shz_trig_sh4.inl.h \
    $(KOS_BASE)/addons/include/sh4zam/inline/sw/shz_trig_sw.inl.h
$(OBJS) $(U16_OBJS) fast-trig-call.o fast-trig-probe.o: $(SHZ_TRIG_HEADERS)

u16-trig-test.elf: $(U16_OBJS)
	kos-cc -fno-lto -o $@ $^

u16-trig-test.o: u16-trig-test.c
	kos-cc -fno-fast-math -fno-lto -c $< -o $@

u16-trig-strict.o: u16-trig-call.c
	kos-cc -O2 -fno-fast-math -fno-lto -DTRIG_CALL=trig_strict -DTRIG_CONSTANT_CALL=trig_strict_constant -c $< -o $@

u16-trig-fast.o: u16-trig-call.c
	kos-cc -O2 -ffast-math -fno-lto -DTRIG_CALL=trig_fast -DTRIG_CONSTANT_CALL=trig_fast_constant -c $< -o $@

u16-trig-ofast.o: u16-trig-call.c
	kos-cc -Ofast -fno-lto -DTRIG_CALL=trig_ofast -DTRIG_CONSTANT_CALL=trig_ofast_constant -c $< -o $@

u16-trig-cpp.o: u16-trig-call.c
	kos-c++ -x c++ -O2 -ffast-math -fno-lto -DTRIG_CALL=trig_cpp -DTRIG_CONSTANT_CALL=trig_cpp_constant -c $< -o $@

clean: rm-elf
	-rm -f $(OBJS)
	-rm -f fast-trig-probe.elf fast-trig-probe.o fast-trig-call.o
	-rm -f u16-trig-test.elf $(U16_OBJS)

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
