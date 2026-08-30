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

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
