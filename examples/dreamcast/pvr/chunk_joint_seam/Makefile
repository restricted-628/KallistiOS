# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

TARGET = chunk-joint-seam.elf
OBJS = chunk-joint-seam.o

.PHONY: all clean run

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a \
           $(KOS_BASE)/addons/lib/$(KOS_ARCH)/libsh4zam.a
	kos-cc -o $@ $(OBJS)

clean:
	-rm -f $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
