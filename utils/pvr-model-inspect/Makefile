# KallistiOS ##version##
#
# utils/pvr-model-inspect/Makefile
# Copyright (C) 2026 Joseph Black
#

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include

TARGET = pvr-model-inspect
SOURCES = pvr-model-inspect.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./test.py ./$(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) $(DC_TOOLS_BASE)/

clean:
	rm -f $(TARGET)
