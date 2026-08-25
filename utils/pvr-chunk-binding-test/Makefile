# KallistiOS ##version##
#
# Host-side compact-model resource-binding tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS += -Iinclude -I../pvr-chunk-render-test/include \
            -I../../kernel/arch/dreamcast/include

TARGET = pvr-chunk-binding-test
SOURCES = pvr-chunk-binding-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_binding.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(TARGET).dSYM
