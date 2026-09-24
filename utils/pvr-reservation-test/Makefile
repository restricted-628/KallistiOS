# KallistiOS ##version##
#
# Host-side contiguous PVR reservation tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include

TARGET = pvr-reservation-test
SOURCES = pvr-reservation-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_mem_reservation.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_texture_reservation.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_texture_layout.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(TARGET).dSYM
