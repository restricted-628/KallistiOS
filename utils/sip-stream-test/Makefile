# KallistiOS ##version##
#
# Host tests for the microphone capture ring.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic

TARGET = sip-stream-test
SOURCE = ../../kernel/arch/dreamcast/hardware/maple/sip_stream.c

.PHONY: all test clean

all: $(TARGET)

$(TARGET): sip-stream-test.c $(SOURCE)
	$(CC) $(CFLAGS) -I../../kernel/arch/dreamcast/hardware/maple \
		-o $@ sip-stream-test.c $(SOURCE)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
