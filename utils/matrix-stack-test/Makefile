# KallistiOS ##version##
#
# Host-side caller-owned matrix stack tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic

KOS_ROOT = ../..
MATH_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/math
INCLUDES = -Iinclude -I$(KOS_ROOT)/kernel/arch/dreamcast/include
TARGET = matrix-stack-test
SOURCES = matrix-stack-test.c $(MATH_DIR)/matrix_stack.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
