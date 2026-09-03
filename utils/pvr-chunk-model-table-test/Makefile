# KallistiOS ##version##
#
# Host-side compact model-table tests.
# Copyright (C) 2026 Joseph Black
#

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-model-test/include \
	-I../pvr-model-convert \
	-I../../kernel/arch/dreamcast/include

TARGET = pvr-chunk-model-table-test
SOURCES = pvr-chunk-model-table-test.c \
	../pvr-model-convert/pvr-model-table-ir.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model_table.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
	rm -rf $(TARGET).dSYM
