# KallistiOS ##version##
#
# Host-side independent-heap tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CFLAGS += -pthread
ROOT := ../..
INCLUDES := -Iinclude
SOURCE := $(ROOT)/kernel/mm/heap.c
TARGET := mm-heap-test

.PHONY: all clean test

all: test

$(TARGET): mm-heap-test.c $(SOURCE) $(ROOT)/include/kos/heap.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ mm-heap-test.c $(SOURCE)

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
