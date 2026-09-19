# KallistiOS ##version##
#
# utils/pvr-cell-convert/Makefile
# Copyright (C) 2026 Joseph Black
#

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-geometry-test/include \
            -I../../kernel/arch/dreamcast/include \
            -I../pvrtex
LDLIBS += -lm

TARGET = pvr-cell-convert
SOURCES = pvr-cell-convert.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_cell.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_cell_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_sprite_geometry.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c \
          ../pvrtex/stb_image_impl.c

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./test.py ./$(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) $(DC_TOOLS_BASE)/

clean:
	rm -f $(TARGET)
	rm -rf $(TARGET).dSYM
