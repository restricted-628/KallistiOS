# KallistiOS ##version##
#
# Direct-disc compact-model asset benchmark
# Copyright (C) 2026 Joseph Black

TARGET = chunk-asset-disc.elf
MODEL_SOURCE = ../chunk_asset/chunk-asset-model.obj
MODEL_ASSET = chunk-asset-model.pcm
MODEL_CONVERTER = $(KOS_BASE)/utils/pvr-model-convert/pvr-model-convert
HOST_CC ?= cc
OBJS = chunk-asset-disc.o

all: rm-elf $(TARGET) $(MODEL_ASSET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS) $(MODEL_ASSET)

rm-elf:
	-rm -f $(TARGET)

$(MODEL_CONVERTER):
	$(MAKE) -C $(KOS_BASE)/utils/pvr-model-convert CC="$(HOST_CC)"

$(MODEL_ASSET): $(MODEL_SOURCE) $(MODEL_CONVERTER)
	$(MODEL_CONVERTER) --join-strips --emit-asset --lz4-vertices \
		$(MODEL_SOURCE) $@

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS) -llz4

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET) $(MODEL_ASSET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
