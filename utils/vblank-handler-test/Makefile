# KallistiOS ##version##
#
# Host-side VBlank handler tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
ROOT := ../..
INCLUDES := -Iinclude -I$(ROOT)/kernel/arch/dreamcast/include
TARGET := vblank-handler-test
OBJS := vblank-handler-test.o vblank.o

.PHONY: all clean test

all: test

vblank.o: $(ROOT)/kernel/arch/dreamcast/hardware/vblank.c
	$(CC) $(CFLAGS) $(INCLUDES) -Dmalloc=vblank_test_malloc \
		-Dfree=vblank_test_free -c -o $@ $<

vblank-handler-test.o: vblank-handler-test.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
	$(RM) -r $(TARGET).dSYM
