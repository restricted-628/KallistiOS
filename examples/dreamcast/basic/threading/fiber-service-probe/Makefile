# KallistiOS ##version##
#
# basic/threading/fiber-service-probe/Makefile
# Copyright (C) 2026 Joseph Black
#

TARGET = fiber-service-probe.elf
OBJS = fiber-service-probe.o fiber-service-math.o
KOS_CFLAGS += -std=gnu17

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS)

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -Wl,-Map,$@.map -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
