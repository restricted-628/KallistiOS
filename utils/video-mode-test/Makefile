# KallistiOS ##version##
#
# Host-side video mode policy and validation tests.

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
ROOT := ../..
INCLUDES := -Iinclude -I$(ROOT)/kernel/arch/dreamcast/hardware
SOURCE := $(ROOT)/kernel/arch/dreamcast/hardware/video_mode.c
TARGET := video-mode-test

.PHONY: all clean test

all: $(TARGET)

$(TARGET): video-mode-test.c $(SOURCE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	-rm -f $(TARGET)
