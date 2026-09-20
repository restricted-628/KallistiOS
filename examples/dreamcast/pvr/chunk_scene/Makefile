# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

TARGET = chunk-scene.elf
MODEL_CONVERTER = $(KOS_BASE)/utils/pvr-model-convert/pvr-model-convert
BIN2C = $(KOS_BASE)/utils/bin2c/bin2c
HOST_CC ?= cc
PYTHON ?= python3
OBJS = chunk-scene.o chunk-scene-asset.o

.PHONY: all clean run dist
all: $(TARGET) chunk-workload.elf chunk-skin-grid.elf

include $(KOS_BASE)/Makefile.rules

$(MODEL_CONVERTER):
	$(MAKE) -C $(KOS_BASE)/utils/pvr-model-convert CC="$(HOST_CC)"

$(BIN2C):
	$(MAKE) -C $(KOS_BASE)/utils/bin2c

chunk-scene.pcm: chunk-scene.gltf $(MODEL_CONVERTER)
	$(MODEL_CONVERTER) --emit-asset --section-directory --cooked-cache $< $@

chunk-scene-asset.c: chunk-scene.pcm $(BIN2C)
	$(BIN2C) $< $@ chunk_scene_asset

chunk-scene-asset.o: chunk-scene-asset.c

skin-grid.gltf: chunk-scene.gltf generate-skin-grid.py
	$(PYTHON) generate-skin-grid.py $< $@

skin-grid.pcm: skin-grid.gltf $(MODEL_CONVERTER)
	$(MODEL_CONVERTER) --emit-asset --section-directory --cooked-cache --join-strips --texture-id 7 $< $@

skin-grid-asset.c: skin-grid.pcm $(BIN2C)
	$(BIN2C) $< $@ skin_grid_asset

chunk-skin-grid.o: chunk-scene.c
	kos-cc $(CFLAGS) $(CPPFLAGS) -DCHUNK_SCENE_GRID -c $< -o $@

chunk-skin-grid.elf: chunk-skin-grid.o skin-grid-asset.o $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ chunk-skin-grid.o skin-grid-asset.o

chunk-scene.o: scene-workload.h

chunk-workload.o: chunk-scene.c scene-workload.h
	kos-cc $(CFLAGS) $(CPPFLAGS) -DCHUNK_SCENE_WORKLOAD -c $< -o $@

chunk-workload.elf: chunk-workload.o chunk-scene-asset.o $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ chunk-workload.o chunk-scene-asset.o

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

clean:
	-rm -f chunk-skin-grid.elf chunk-skin-grid.o skin-grid-asset.o skin-grid-asset.c skin-grid.pcm skin-grid.gltf
	-rm -f $(TARGET) $(OBJS) chunk-workload.elf chunk-workload.o chunk-scene.pcm chunk-scene-asset.c

dist: all
	-rm -f chunk-skin-grid.o skin-grid-asset.o skin-grid-asset.c skin-grid.pcm skin-grid.gltf
	-rm -f $(OBJS) chunk-workload.o chunk-scene.pcm chunk-scene-asset.c
	$(KOS_STRIP) $(TARGET) chunk-workload.elf chunk-skin-grid.elf
