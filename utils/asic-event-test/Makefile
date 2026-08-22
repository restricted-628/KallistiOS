# KallistiOS ##version##
#
# Host-side ASIC event ownership tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
ROOT := ../..
INCLUDES := -Iinclude -I$(ROOT)/kernel/arch/dreamcast/include
TARGET := asic-event-test
OBJS := asic-event-test.o asic.o

.PHONY: all clean test

all: test

asic.o: $(ROOT)/kernel/arch/dreamcast/hardware/asic.c asic-test-shim.h
	$(CC) $(CFLAGS) $(INCLUDES) -include asic-test-shim.h \
		-DASIC_MMIO_READ32=asic_test_read32 \
		-DASIC_MMIO_WRITE32=asic_test_write32 -c -o $@ $<

asic-event-test.o: asic-event-test.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
	$(RM) -r $(TARGET).dSYM
