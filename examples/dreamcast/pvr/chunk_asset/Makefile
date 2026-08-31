# KallistiOS ##version##
#
# Build-generated compact-model asset example
# Copyright (C) 2026 Joseph Black

TARGET = chunk-asset.elf
MODEL_SOURCE = chunk-asset-model.obj
MODEL_ASSET = chunk-asset-model.pcm
GENERATED_MODEL = chunk-asset-model.c
MODEL_CONVERTER = $(KOS_BASE)/utils/pvr-model-convert/pvr-model-convert
BIN2C = $(KOS_BASE)/utils/bin2c/bin2c
HOST_CC ?= cc
OBJS = chunk-asset.o chunk-asset-model.o

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS) $(MODEL_ASSET) $(GENERATED_MODEL)

rm-elf:
	-rm -f $(TARGET)

$(MODEL_CONVERTER):
	$(MAKE) -C $(KOS_BASE)/utils/pvr-model-convert CC="$(HOST_CC)"

$(BIN2C):
	$(MAKE) -C $(KOS_BASE)/utils/bin2c

$(MODEL_ASSET): $(MODEL_SOURCE) $(MODEL_CONVERTER)
	$(MODEL_CONVERTER) --join-strips --emit-asset --section-directory \
		--scene-root --rigid-skin --lz4-vertices \
		$(MODEL_SOURCE) $@

$(GENERATED_MODEL): $(MODEL_ASSET) $(BIN2C)
	$(BIN2C) $(MODEL_ASSET) $@ chunk_asset_model

chunk-asset-model.o: $(GENERATED_MODEL)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS) -llz4

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS) $(MODEL_ASSET) $(GENERATED_MODEL)
	$(KOS_STRIP) $(TARGET)
