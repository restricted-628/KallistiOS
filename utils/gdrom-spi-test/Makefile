# Host-side golden-vector tests for the pure GD-ROM SPI packet encoder.

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test

HARDWARE_DIR = ../../kernel/arch/dreamcast/hardware
TARGET = gdrom-spi-test
SOURCES = gdrom-spi-test.c $(HARDWARE_DIR)/gdrom_spi.c

.PHONY: all test clean

all: test

$(TARGET): $(SOURCES) $(HARDWARE_DIR)/gdrom_spi.h
	$(CC) $(CFLAGS) -I$(HARDWARE_DIR) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
