# Host-side VMU bank-command protocol tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test

KOS_ROOT = ../..
MAPLE_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/hardware/maple
TARGET = vmu-bank-test
SOURCES = vmu-bank-test.c $(MAPLE_DIR)/vmu_bank_protocol.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES) $(MAPLE_DIR)/vmu_bank_protocol.h
	$(CC) $(CFLAGS) -I$(MAPLE_DIR) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
