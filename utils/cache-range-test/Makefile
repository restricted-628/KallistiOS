# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2
HOST_CSTD ?= gnu17
CFLAGS += -std=$(HOST_CSTD) -Wall -Wextra -Werror $(HOST_PEDANTIC)
ROOT := ../..
INCLUDES := -Iinclude -I$(ROOT)/kernel/arch/dreamcast/include
.PHONY: all test clean
all: test
cache-range-test: cache-range-test.c $(ROOT)/kernel/arch/dreamcast/include/arch/cache_range.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $<
test: cache-range-test
	./cache-range-test
	$(PYTHON) asm-entry-test.py $(ROOT)/kernel/arch/dreamcast/kernel/cache.s
clean:
	$(RM) cache-range-test
