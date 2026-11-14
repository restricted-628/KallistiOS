CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
TARGET = sh4der-ao-test
all: $(TARGET) cook
$(TARGET): test.c ../../addons/libsh4der/kos/ao.c ../../addons/include/sh4der_kos_ao.h
	$(CC) $(CFLAGS) $(CPPFLAGS) test.c ../../addons/libsh4der/kos/ao.c -lm -o $@
cook: cook.c
	$(CC) $(CFLAGS) $(CPPFLAGS) cook.c -lm -o $@
test: all
	./$(TARGET)
	./cook > cooked.actual.h
	cmp cooked.h cooked.actual.h
dreamcast:
	kos-cc native.c -lsh4der -o sh4der-ao-test.elf
clean:
	$(RM) $(TARGET) cook cooked.actual.h sh4der-ao-test.elf
