# KallistiOS ##version##
#
# Caller-owned animation playback example.
# Copyright (C) 2026 Joseph Black

TARGET = animation-playback.elf
OBJS = animation-playback.o
KOS_CFLAGS += -std=c11

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS)

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)

.PHONY: all clean rm-elf run dist
