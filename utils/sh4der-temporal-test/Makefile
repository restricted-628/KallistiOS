CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../sh4der-lens-test/include -I../pvr-reservation-test/include -I../../kernel/arch/dreamcast/include
TARGET = sh4der-temporal-test
SOURCES = test.c ../../addons/libsh4der/kos/image.c ../../addons/libsh4der/kos/overlay.c ../../addons/libsh4der/kos/trail.c ../../addons/libsh4der/kos/frame.c ../../addons/libsh4der/kos/look.c ../../addons/libsh4der/kos/screen.c ../../addons/libsh4der/kos/glow.c
all: $(TARGET)
$(TARGET): $(SOURCES) ../../addons/include/sh4der_kos_trail.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast:
	kos-cc native.c -lsh4der -o sh4der-temporal-test.elf
	kos-cc look-native.c -lsh4der -o sh4der-look-test.elf
clean:
	$(RM) $(TARGET) sh4der-temporal-test.elf sh4der-look-test.elf
