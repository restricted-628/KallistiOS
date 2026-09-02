# KallistiOS ##version##
#
# Authored compact-model opacity routing example
# Copyright (C) 2026 Joseph Black

TARGET = chunk-opacity.elf
MODEL_SOURCE = chunk-opacity.gltf
MODEL_ASSET = chunk-opacity.pcm
GENERATED_ASSET = chunk-opacity-asset.c
MODEL_CONVERTER = $(KOS_BASE)/utils/pvr-model-convert/pvr-model-convert
BIN2C = $(KOS_BASE)/utils/bin2c/bin2c
HOST_CC ?= cc
OBJS = chunk-opacity.o chunk-opacity-asset.o

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS) $(MODEL_ASSET) $(GENERATED_ASSET)

rm-elf:
	-rm -f $(TARGET)

$(MODEL_CONVERTER):
	$(MAKE) -C $(KOS_BASE)/utils/pvr-model-convert CC="$(HOST_CC)"

$(BIN2C):
	$(MAKE) -C $(KOS_BASE)/utils/bin2c

$(MODEL_ASSET): $(MODEL_SOURCE) $(MODEL_CONVERTER)
	$(MODEL_CONVERTER) --join-strips --emit-asset \
		--section-directory $(MODEL_SOURCE) $@

$(GENERATED_ASSET): $(MODEL_ASSET) $(BIN2C)
	$(BIN2C) $(MODEL_ASSET) $@ chunk_opacity_asset

chunk-opacity-asset.o: $(GENERATED_ASSET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
