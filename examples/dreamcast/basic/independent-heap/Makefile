# KallistiOS ##version##
#
# examples/dreamcast/basic/independent-heap/Makefile
# Copyright (C) 2026 Joseph Black

TARGET = independent-heap.elf
OBJS = independent-heap.o

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS) heap-safety.o heap-safety.elf

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

heap-safety.elf: heap-safety.o
	kos-cc -o $@ $<

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)

.PHONY: all clean rm-elf run dist
