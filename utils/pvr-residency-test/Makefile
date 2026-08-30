# KallistiOS ##version##
#
# Host-side fixed-slot PVR texture residency tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include

TARGET = pvr-residency-test
SOURCES = pvr-residency-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_mem_reservation.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_texture_layout.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_texture_reservation.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_texture_residency.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(TARGET).dSYM
