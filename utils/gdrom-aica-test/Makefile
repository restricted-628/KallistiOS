CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test

TARGET := gdrom-aica-test
DESTINATION_TARGET := gdrom-destination-test
LOAD_TARGET := gdrom-owned-load-test
HARDWARE := ../../kernel/arch/dreamcast/hardware

.PHONY: all test clean
all: test

$(TARGET): test.c $(HARDWARE)/gdrom_aica_poll.h
	$(CC) $(CFLAGS) -I$(HARDWARE) -o $@ $<

$(DESTINATION_TARGET): destination.c $(HARDWARE)/gdrom_dma_destination.h $(HARDWARE)/dma_memory.h
	$(CC) $(CFLAGS) -I$(HARDWARE) -o $@ $<

$(LOAD_TARGET): owned.c $(HARDWARE)/gdrom_aica_load.inc
	$(CC) $(CFLAGS) -I$(HARDWARE) -o $@ $<

test: $(TARGET) $(DESTINATION_TARGET) $(LOAD_TARGET)
	./$(TARGET)
	./$(DESTINATION_TARGET)
	./$(LOAD_TARGET)

clean:
	$(RM) $(TARGET) $(DESTINATION_TARGET) $(LOAD_TARGET)
