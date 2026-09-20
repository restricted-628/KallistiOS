# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

TARGET = chunk-scene.elf
MODEL_CONVERTER = $(KOS_BASE)/utils/pvr-model-convert/pvr-model-convert
BIN2C = $(KOS_BASE)/utils/bin2c/bin2c
HOST_CC ?= cc
OBJS = chunk-scene.o chunk-scene-asset.o

.PHONY: all clean run dist
all: $(TARGET) chunk-workload.elf

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
	-rm -f $(TARGET) $(OBJS) chunk-workload.elf chunk-workload.o chunk-scene.pcm chunk-scene-asset.c

dist: all
	-rm -f $(OBJS) chunk-workload.o chunk-scene.pcm chunk-scene-asset.c
	$(KOS_STRIP) $(TARGET) chunk-workload.elf
