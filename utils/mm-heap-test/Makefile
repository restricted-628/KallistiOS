# KallistiOS ##version##
#
# Host-side independent-heap tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
HOST_CSTD ?= gnu17
HOST_PEDANTIC ?=
CFLAGS += -std=$(HOST_CSTD) -Wall -Wextra -Werror $(HOST_PEDANTIC) -pthread
ROOT := ../..
INCLUDES := -Iinclude
SOURCE := $(ROOT)/kernel/mm/heap.c
TARGET := mm-heap-test heap-corruption-test

.PHONY: all clean test

all: test

mm-heap-test: mm-heap-test.c $(SOURCE) $(ROOT)/include/kos/heap.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ mm-heap-test.c $(SOURCE)

heap-corruption-test: heap-corruption-test.c $(SOURCE) $(ROOT)/include/kos/heap.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ heap-corruption-test.c

test: $(TARGET)
	./mm-heap-test
	./heap-corruption-test

clean:
	$(RM) $(TARGET)
