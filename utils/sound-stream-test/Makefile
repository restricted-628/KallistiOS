# KallistiOS ##version##
#
# Host-side exact stream-tail tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../../kernel/arch/dreamcast/sound

TARGET = sound-stream-test

.PHONY: all clean test

all: test

$(TARGET): sound-stream-test.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) -r $(TARGET) $(TARGET).dSYM
