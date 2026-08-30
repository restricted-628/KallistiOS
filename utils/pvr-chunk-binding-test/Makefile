# KallistiOS ##version##
#
# Host-side compact-model resource-binding tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../pvr-chunk-render-test/include \
            -I../../kernel/arch/dreamcast/include

TARGET = pvr-chunk-binding-test
SOURCES = pvr-chunk-binding-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_binding.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_lighting.c
LDLIBS += -lm

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(TARGET).dSYM
