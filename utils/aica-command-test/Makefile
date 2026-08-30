# KallistiOS ##version##
#
# Host-side AICA shared-command layout tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../../kernel/arch/dreamcast/include

TARGET = aica-command-test

.PHONY: all clean test

all: test

$(TARGET): aica-command-test.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) -r $(TARGET) $(TARGET).dSYM
