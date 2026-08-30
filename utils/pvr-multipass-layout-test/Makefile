# KallistiOS ##version##
#
# Host-side PVR multipass layout tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test

KOS_ROOT = ../..
PVR_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/hardware/pvr
INCLUDES = -I$(PVR_DIR)
TARGET = pvr-multipass-layout-test
SOURCES = pvr-multipass-layout-test.c $(PVR_DIR)/pvr_multipass_layout.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
