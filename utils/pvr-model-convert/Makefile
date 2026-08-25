# KallistiOS ##version##
#
# utils/pvr-model-convert/Makefile
# Copyright (C) 2026 Joseph Black
#

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-model-convert
SOURCES = pvr-model-convert.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	$(MAKE) -C ../pvr-model-inspect
	./test.py ./$(TARGET) ../pvr-model-inspect/pvr-model-inspect

install: $(TARGET)
	install -m 755 $(TARGET) $(DC_TOOLS_BASE)/

clean:
	rm -f $(TARGET)
