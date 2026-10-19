# KallistiOS ##version##
#
# Topology-aware compact-model band-shading example
# Copyright (C) 2026 Joseph Black

TARGET = chunk-toon.elf
OBJS = showcase.o

.PHONY: all clean run dist smoke triangle styles styles-smoke

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS) showcase-smoke.o chunk-toon.o chunk-toon-smoke.elf chunk-toon-triangle.elf
	-rm -f showcase-styles.o showcase-styles-smoke.o chunk-styles.elf chunk-styles-smoke.elf

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $(TARGET) $(OBJS)

smoke: chunk-toon-smoke.elf

showcase-smoke.o: showcase.c
	kos-cc $(KOS_CFLAGS) -DTOON_SMOKE -c $< -o $@

chunk-toon-smoke.elf: showcase-smoke.o $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ showcase-smoke.o

styles: chunk-styles.elf

showcase-styles.o: showcase.c
	kos-cc $(KOS_CFLAGS) -DSHOWCASE_START_STYLE=1 -c $< -o $@

chunk-styles.elf: showcase-styles.o $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ showcase-styles.o

styles-smoke: chunk-styles-smoke.elf

showcase-styles-smoke.o: showcase.c
	kos-cc $(KOS_CFLAGS) -DSTYLES_SMOKE -c $< -o $@

chunk-styles-smoke.elf: showcase-styles-smoke.o $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ showcase-styles-smoke.o

triangle: chunk-toon-triangle.elf

chunk-toon-triangle.elf: chunk-toon.o $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ chunk-toon.o

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
