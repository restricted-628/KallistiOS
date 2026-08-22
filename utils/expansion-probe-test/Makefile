# KallistiOS ##version##
#
# Host tests for expansion capability discovery.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic

TARGET = expansion-probe-test
SOURCE = ../../kernel/arch/dreamcast/hardware/expansion.c
DC_INCLUDE = ../../kernel/arch/dreamcast/include

.PHONY: all test clean

all: $(TARGET)

$(TARGET): expansion-probe-test.c $(SOURCE)
	$(CC) $(CFLAGS) -Iinclude -I$(DC_INCLUDE) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
