# KallistiOS ##version##
#
# Explicit compact-model skinning example
# Copyright (C) 2026 Joseph Black

TARGET = chunk-skin.elf
OBJS = chunk-skin.o
COMPACT_TARGET = chunk-skin-compact.elf
COMPACT_OBJS = chunk-skin-compact.o

.PHONY: all clean run dist

all: $(TARGET) $(COMPACT_TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS) $(COMPACT_TARGET) $(COMPACT_OBJS)

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $(TARGET) $(OBJS)

chunk-skin-compact.o: chunk-skin.c
	kos-cc $(KOS_CFLAGS) -DCHUNK_SKIN_COMPACT -c $< -o $@

$(COMPACT_TARGET): $(COMPACT_OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(COMPACT_OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET) $(COMPACT_TARGET)
	-rm -f $(OBJS) $(COMPACT_OBJS)
	$(KOS_STRIP) $(TARGET) $(COMPACT_TARGET)
