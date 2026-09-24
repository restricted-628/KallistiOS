# Host-side physical main-RAM DMA admission tests.
CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
HARDWARE_DIR = ../../kernel/arch/dreamcast/hardware
TARGET = dma-memory-test

.PHONY: all test clean
all: test
$(TARGET): dma-memory-test.c $(HARDWARE_DIR)/dma_memory.h
	$(CC) $(CFLAGS) -I$(HARDWARE_DIR) $< -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	$(RM) $(TARGET)
