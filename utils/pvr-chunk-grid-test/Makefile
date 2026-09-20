# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
CC ?= cc
CFLAGS ?= -O2
PYTHON ?= python3
include ../Makefile.host-test
CPPFLAGS += -DGRID_HOST -I../pvr-chunk-render-test/include -I../../kernel/arch/dreamcast/include
LDLIBS += -lm
TARGET = pvr-chunk-grid-test
EXAMPLE = ../../examples/dreamcast/pvr/chunk_grid
PVR = ../../kernel/arch/dreamcast/hardware/pvr
CONVERTER = ../pvr-model-convert/pvr-model-convert
SOURCES = $(EXAMPLE)/chunk-grid.c ../pvr-chunk-scene-integration-test/host-support.c \
          $(PVR)/pvr_chunk_asset.c $(PVR)/pvr_chunk_model.c \
          $(PVR)/pvr_chunk_render.c $(PVR)/pvr_geometry.c $(PVR)/pvr_frustum.c

.PHONY: all test clean
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

$(CONVERTER):
	$(MAKE) -C ../pvr-model-convert

grid.obj: $(EXAMPLE)/generate-grid.py
	$(PYTHON) $< $@

grid.pcm: grid.obj $(CONVERTER)
	$(CONVERTER) --emit-asset --section-directory --join-strips $< $@

test: $(TARGET) grid.pcm
	$(PYTHON) test.py ./$(TARGET) grid.pcm

clean:
	rm -f $(TARGET) grid.obj grid.pcm
