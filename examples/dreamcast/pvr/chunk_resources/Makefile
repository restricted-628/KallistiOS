# KallistiOS ##version##
#
# Build-generated compact-model resource-binding example
# Copyright (C) 2026 Joseph Black

TARGET = chunk-resources.elf
MODEL_SOURCE = chunk-resource-model.obj
MODEL_MATERIALS = chunk-resource-model.mtl
GENERATED_MODEL = chunk-resource-model.c
MODEL_CONVERTER = $(KOS_BASE)/utils/pvr-model-convert/pvr-model-convert
HOST_CC ?= cc
OBJS = chunk-resources.o chunk-resource-model.o

.PHONY: all clean run dist

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS) $(GENERATED_MODEL)

$(MODEL_CONVERTER):
	$(MAKE) -C $(KOS_BASE)/utils/pvr-model-convert CC="$(HOST_CC)"

$(GENERATED_MODEL): $(MODEL_SOURCE) $(MODEL_MATERIALS) $(MODEL_CONVERTER)
	$(MODEL_CONVERTER) --join-strips --material checker=7 \
		--material-library $(MODEL_MATERIALS) \
		--emit-c chunk_resource_model $(MODEL_SOURCE) $@

chunk-resource-model.o: $(GENERATED_MODEL)

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
