# Host-side VMU filesystem metadata validation tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test

KOS_ROOT = ../..
VMUFS_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/fs
INCLUDES = -I$(KOS_ROOT)/kernel/arch/dreamcast/include -I$(VMUFS_DIR)
TARGET = vmufs-validate-test
SOURCES = vmufs-validate-test.c $(VMUFS_DIR)/vmufs_validate.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES) \
           $(KOS_ROOT)/kernel/arch/dreamcast/include/dc/vmufs_meta.h \
           $(VMUFS_DIR)/vmufs_internal.h
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
