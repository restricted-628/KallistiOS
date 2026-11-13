CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../pvr-reservation-test/include -I../../kernel/arch/dreamcast/include
TARGET = sh4der-frame-test
SOURCES = test.c ../../addons/libsh4der/kos/frame.c
all: $(TARGET)
$(TARGET): $(SOURCES) ../../addons/include/sh4der_kos_frame.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast:
	kos-cc native.c -lsh4der -o sh4der-frame-test.elf
clean:
	$(RM) $(TARGET) sh4der-frame-test.elf
