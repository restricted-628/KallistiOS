# KallistiOS ##version##
#
# Host-side GAPS SRAM ownership tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
ROOT := ../..
INCLUDES := -I../g2dma-test/include \
	-I$(ROOT)/kernel/arch/dreamcast/include \
	-I$(ROOT)/kernel/arch/dreamcast/hardware
TARGET := gaps-sram-test
OBJS := gaps-sram-test.o gaps.o

.PHONY: all clean test

all: test

gaps.o: $(ROOT)/kernel/arch/dreamcast/hardware/gaps.c \
	$(ROOT)/kernel/arch/dreamcast/hardware/gaps_internal.h
	$(CC) $(CFLAGS) $(INCLUDES) \
		-include ../g2dma-test/g2dma-test-shim.h -c -o $@ $<

gaps-sram-test.o: gaps-sram-test.c \
	$(ROOT)/kernel/arch/dreamcast/hardware/gaps_internal.h
	$(CC) $(CFLAGS) $(INCLUDES) \
		-include ../g2dma-test/g2dma-test-shim.h -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
	$(RM) -r $(TARGET).dSYM
