# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

TARGET = tilemap.elf
OBJS = tilemap.o

.PHONY: all clean run dist
all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS) -lm

clean:
	$(RM) $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	$(RM) $(OBJS)
	$(KOS_STRIP) $(TARGET)
