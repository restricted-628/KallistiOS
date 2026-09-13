# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-binding-test/include \
            -I../pvr-chunk-render-test/include \
            -I../../kernel/arch/dreamcast/include
TARGET = pvr-chunk-layer-test
SOURCES = pvr-chunk-layer-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_layer_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c
HEADERS = ../../kernel/arch/dreamcast/include/dc/pvr_chunk_layer_asset.h \
          ../../kernel/arch/dreamcast/include/dc/pvr_chunk_layer.h \
          ../../kernel/arch/dreamcast/include/dc/pvr_material_types.h \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_layer_internal.h
.PHONY: all test clean dreamcast
all: $(TARGET)
$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast: pvr-chunk-layer-test.elf
pvr-chunk-layer-test.elf: pvr-chunk-layer-test.c $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc $(KOS_CSTD) -Wall -Wextra $< -lm -o $@
clean:
	$(RM) $(TARGET) pvr-chunk-layer-test.elf
