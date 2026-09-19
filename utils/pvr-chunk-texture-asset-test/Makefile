# KallistiOS ##version##
#
# Host-side compact texture-section tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include

TARGET = pvr-chunk-texture-asset-test
SOURCES = pvr-chunk-texture-asset-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_texture_asset.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(TARGET).dSYM
