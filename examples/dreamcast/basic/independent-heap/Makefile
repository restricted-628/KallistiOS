# KallistiOS ##version##
#
# examples/dreamcast/basic/independent-heap/Makefile
# Copyright (C) 2026 Joseph Black

TARGET = independent-heap.elf
OBJS = independent-heap.o
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
