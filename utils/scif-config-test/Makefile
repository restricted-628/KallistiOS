# KallistiOS ##version##
#
# Host-side SCIF configuration tests.

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
ROOT := ../..
INCLUDES := -Iinclude -I$(ROOT)/kernel/arch/dreamcast/hardware
SOURCE := $(ROOT)/kernel/arch/dreamcast/hardware/scif_config.c
TARGET := scif-config-test

.PHONY: all clean test

all: $(TARGET)

$(TARGET): scif-config-test.c $(SOURCE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	-rm -f $(TARGET)
