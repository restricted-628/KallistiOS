# Host-side VMU maintenance-model regression tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic

KOS_ROOT = ../..
VMUFS_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/fs
INCLUDES = -I$(KOS_ROOT)/kernel/arch/dreamcast/include -I$(VMUFS_DIR)
TARGET = vmufs-maintenance-test
SOURCES = vmufs-maintenance-test.c \
	$(VMUFS_DIR)/vmufs_maintenance_model.c \
	$(VMUFS_DIR)/vmufs_defrag_schedule.c \
	$(VMUFS_DIR)/vmufs_validate.c \
	$(VMUFS_DIR)/vmufs_chain.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES) $(VMUFS_DIR)/vmufs_internal.h
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
