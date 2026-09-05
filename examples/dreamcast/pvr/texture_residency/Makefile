# KallistiOS ##version##
#
# Fixed-slot texture residency example
# Copyright (C) 2026 Joseph Black
#

TARGET = texture-residency.elf
OBJS = texture_residency.o

.PHONY: all clean run dist

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS)

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
