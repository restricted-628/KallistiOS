# KallistiOS ##version##
#
# Build-generated cell-sprite asset and material-routing example
# Copyright (C) 2026 Joseph Black

TARGET = cell-asset.elf
MANIFEST = cell-asset.pcell
ATLAS_IMAGE = cell-atlas.png
CELL_ASSET = cell-asset.pca
GENERATED_ATLAS = cell-asset-atlas.c
GENERATED_ASSET = cell-asset-data.c
CELL_CONVERTER = $(KOS_BASE)/utils/pvr-cell-convert/pvr-cell-convert
BIN2C = $(KOS_BASE)/utils/bin2c/bin2c
HOST_CC ?= cc
PYTHON ?= python3
OBJS = cell-asset.o cell-asset-atlas.o cell-asset-data.o

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS) $(ATLAS_IMAGE) $(CELL_ASSET) \
		$(GENERATED_ATLAS) $(GENERATED_ASSET)

rm-elf:
	-rm -f $(TARGET)

$(CELL_CONVERTER):
	$(MAKE) -C $(KOS_BASE)/utils/pvr-cell-convert CC="$(HOST_CC)"

$(BIN2C):
	$(MAKE) -C $(KOS_BASE)/utils/bin2c

$(ATLAS_IMAGE): make-atlas.py
	$(PYTHON) $< $@

$(CELL_ASSET): $(MANIFEST) $(ATLAS_IMAGE) $(CELL_CONVERTER)
	$(CELL_CONVERTER) --image $(ATLAS_IMAGE) \
		--symbol cell_asset_atlas $(MANIFEST) $@ $(GENERATED_ATLAS)

$(GENERATED_ATLAS): $(CELL_ASSET)
	@test -f $@

$(GENERATED_ASSET): $(CELL_ASSET) $(BIN2C)
	$(BIN2C) $(CELL_ASSET) $@ cell_asset

cell-asset-atlas.o: $(GENERATED_ATLAS)
cell-asset-data.o: $(GENERATED_ASSET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
