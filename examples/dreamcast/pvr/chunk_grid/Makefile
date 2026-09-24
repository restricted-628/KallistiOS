# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

TARGET = chunk-grid.elf
OBJS = chunk-grid.o grid-asset.o
PYTHON ?= python3
HOST_CC ?= cc
CONVERTER = $(KOS_BASE)/utils/pvr-model-convert/pvr-model-convert
BIN2C = $(KOS_BASE)/utils/bin2c/bin2c

.PHONY: all clean run
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

$(CONVERTER):
	$(MAKE) -C $(KOS_BASE)/utils/pvr-model-convert CC="$(HOST_CC)"

$(BIN2C):
	$(MAKE) -C $(KOS_BASE)/utils/bin2c

grid.obj: generate-grid.py
	$(PYTHON) $< $@

grid.pcm: grid.obj $(CONVERTER)
	$(CONVERTER) --emit-asset --section-directory --join-strips $< $@

grid-asset.c: grid.pcm $(BIN2C)
	$(BIN2C) $< $@ grid_asset

grid-asset.o: grid-asset.c

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

clean:
	-rm -f $(TARGET) $(OBJS) grid.obj grid.pcm grid-asset.c
