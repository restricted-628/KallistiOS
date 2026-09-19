# KallistiOS ##version##
#
# Host-side G2 DMA tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
ROOT := ../..
INCLUDES := -Iinclude -I$(ROOT)/kernel/arch/dreamcast/include \
	-I$(ROOT)/kernel/arch/dreamcast/hardware
TARGET := g2dma-test
OBJS := g2dma-test.o g2dma.o

.PHONY: all clean test

all: test

g2dma.o: $(ROOT)/kernel/arch/dreamcast/hardware/g2dma.c \
	$(ROOT)/kernel/arch/dreamcast/hardware/gaps_internal.h g2dma-test-shim.h
	$(CC) $(CFLAGS) $(INCLUDES) -include g2dma-test-shim.h -c -o $@ $<

g2dma-test.o: g2dma-test.c \
	$(ROOT)/kernel/arch/dreamcast/hardware/gaps_internal.h g2dma-test-shim.h
	$(CC) $(CFLAGS) $(INCLUDES) -include g2dma-test-shim.h -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
	$(RM) -r $(TARGET).dSYM
