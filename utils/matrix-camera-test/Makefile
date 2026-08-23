# KallistiOS ##version##
#
# Host-side explicit camera matrix tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic

KOS_ROOT = ../..
MATH_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/math
INCLUDES = -Iinclude -I$(KOS_ROOT)/kernel/arch/dreamcast/include
TARGET = matrix-camera-test
SOURCES = matrix-camera-test.c $(MATH_DIR)/matrix_compose.c \
          $(MATH_DIR)/matrix_camera.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -lm -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
