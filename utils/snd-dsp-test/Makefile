# KallistiOS ##version##
#
# Host-side checked DSP program validation tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test

DREAMCAST_INCLUDE = ../../kernel/arch/dreamcast/include
SOUND_DIR = ../../kernel/arch/dreamcast/sound
TARGET = snd-dsp-test
SOURCES = snd-dsp-test.c $(SOUND_DIR)/snd_dsp_program.c

.PHONY: all clean test

all: test

$(TARGET): $(SOURCES) $(DREAMCAST_INCLUDE)/dc/sound/dsp.h
	$(CC) $(CFLAGS) -Iinclude -I$(DREAMCAST_INCLUDE) \
		$(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) -r $(TARGET) $(TARGET).dSYM
