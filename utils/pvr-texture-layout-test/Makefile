# KallistiOS ##version##
#
# Host-side PVR texture surface layout tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic

KOS_ROOT = ../..
PVR_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/hardware/pvr
INCLUDES = -Iinclude -I$(KOS_ROOT)/kernel/arch/dreamcast/include
TARGET = pvr-texture-layout-test
SOURCES = pvr-texture-layout-test.c $(PVR_DIR)/pvr_texture_layout.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
