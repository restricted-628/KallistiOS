# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-render-test/include \
            -I../pvr-model-convert \
            -I../../kernel/arch/dreamcast/include
TARGET = pvr-chunk-uv-asset-test
SOURCES = pvr-chunk-uv-asset-test.c \
          ../pvr-model-convert/pvr-uv-ir.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_uv_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_layer_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_render.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum.c
HEADERS = ../../kernel/arch/dreamcast/include/dc/pvr_chunk_uv_asset.h \
          ../pvr-model-convert/pvr-uv-ir.h
.PHONY: all test clean dreamcast
all: $(TARGET)
$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast: pvr-chunk-uv-asset-test.elf
pvr-chunk-uv-asset-test.elf: pvr-chunk-uv-asset-test.c \
                            ../pvr-model-convert/pvr-uv-ir.c \
                            $(HEADERS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc $(KOS_CSTD) -Wall -Wextra -Werror -I../pvr-model-convert \
		$< ../pvr-model-convert/pvr-uv-ir.c -lm -o $@
clean:
	$(RM) $(TARGET) pvr-chunk-uv-asset-test.elf
