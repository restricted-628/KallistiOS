# Host-side VMU chain and package codec tests.
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic

KOS_ROOT = ../..
VMUFS_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/fs
VMU_UTIL_DIR = $(KOS_ROOT)/kernel/arch/dreamcast/util
INCLUDES = -I$(KOS_ROOT)/kernel/arch/dreamcast/include -I$(VMUFS_DIR)
TARGET = vmu-storage-test
SOURCES = vmu-storage-test.c $(VMUFS_DIR)/vmufs_chain.c \
	$(VMUFS_DIR)/vmufs_validate.c $(VMU_UTIL_DIR)/vmu_pkg_codec.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES) \
	$(KOS_ROOT)/kernel/arch/dreamcast/include/dc/vmufs_meta.h \
	$(KOS_ROOT)/kernel/arch/dreamcast/include/dc/vmu_pkg.h
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
