# KallistiOS ##version##
#
# Host-side shared G1 ownership tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
ROOT := ../..
INCLUDES := -Iinclude -I$(ROOT)/kernel/arch/dreamcast/hardware
TARGET := g1-bus-test
OBJS := g1-bus-test.o g1_bus.o

.PHONY: all clean test

all: test

g1_bus.o: $(ROOT)/kernel/arch/dreamcast/hardware/g1_bus.c g1-bus-test-shim.h
	$(CC) $(CFLAGS) $(INCLUDES) -include g1-bus-test-shim.h -c -o $@ $<

g1-bus-test.o: g1-bus-test.c g1-bus-test-shim.h
	$(CC) $(CFLAGS) $(INCLUDES) -include g1-bus-test-shim.h -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
	$(RM) -r $(TARGET).dSYM
