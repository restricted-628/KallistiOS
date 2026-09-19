# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-geometry-test/include \
            -I../../kernel/arch/dreamcast/include
LDLIBS += -lm
TARGET = pvr-tilemap-test
SOURCES = pvr-tilemap-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_tilemap.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_cell.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_sprite_geometry.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum.c
HEADERS = $(addprefix ../../kernel/arch/dreamcast/include/dc/, \
          pvr_tilemap.h pvr_cell.h pvr_sprite_geometry.h pvr_geometry.h \
          pvr_frustum.h pvr.h)

.PHONY: all test clean dreamcast
all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

dreamcast: pvr-tilemap-test.elf

pvr-tilemap-test.elf: pvr-tilemap-test.c $(HEADERS) \
                     $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc $(KOS_CSTD) -Wall -Wextra $< -lm -o $@

clean:
	$(RM) $(TARGET) pvr-tilemap-test.elf
