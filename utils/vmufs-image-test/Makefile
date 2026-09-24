# Host-side VMU filesystem image validator and regression tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CFLAGS += -Wno-error=cpp

KOS_ROOT = ../..
VMUFS_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/fs
INCLUDES = -I$(KOS_ROOT)/kernel/arch/dreamcast/include -I$(VMUFS_DIR)
TARGET = vmufs-image-test
SOURCES = vmufs-image-test.c $(VMUFS_DIR)/vmufs_validate.c \
	$(VMUFS_DIR)/vmufs_chain.c \
	$(VMUFS_DIR)/vmufs_maintenance_model.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES) $(KOS_ROOT)/kernel/arch/dreamcast/include/dc/vmufs_meta.h
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET) --self-test

clean:
	$(RM) $(TARGET)
