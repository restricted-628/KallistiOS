CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../addons/libsh4der/upstream/sh4dow-dc/include
TARGET = sh4der-screen-test
SOURCES = test.c ../../addons/libsh4der/kos/screen.c
all: $(TARGET)
$(TARGET): $(SOURCES) ../../addons/include/sh4der_kos_screen.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast:
	kos-cc test.c -lsh4der -o sh4der-screen-test.elf
clean:
	$(RM) $(TARGET) sh4der-screen-test.elf
