# KallistiOS ##version##
#
# Host-side checked PVR material tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include

TARGET = pvr-material-test
SOURCES = pvr-material-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_material.c

.PHONY: all clean test dreamcast

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

# Run on the target to exercise the real compiler, not the host material mocks.
dreamcast: header-defaults.elf

header-defaults.elf: header-defaults.c
	kos-cc $(KOS_CSTD) -Wall -Wextra -Werror $< -o $@

clean:
	$(RM) $(TARGET) header-defaults.elf
