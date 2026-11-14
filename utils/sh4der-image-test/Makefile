CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../pvr-reservation-test/include -I../../kernel/arch/dreamcast/include
TARGET = sh4der-image-test
SOURCES = test.c ../../addons/libsh4der/kos/image.c ../../addons/libsh4der/kos/focus.c ../../addons/libsh4der/kos/warp.c ../../addons/libsh4der/kos/ca.c ../../addons/libsh4der/kos/shafts.c
all: $(TARGET)
$(TARGET): $(SOURCES) ../../addons/include/sh4der_kos_image.h ../../addons/include/sh4der_kos_focus.h ../../addons/include/sh4der_kos_warp.h ../../addons/include/sh4der_kos_ca.h ../../addons/include/sh4der_kos_shafts.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast:
	kos-cc native.c -lsh4der -o sh4der-image-test.elf
	kos-cc warp-native.c -lsh4der -o sh4der-warp-test.elf
	kos-cc ca-shafts-native.c -lsh4der -o sh4der-ca-shafts-test.elf
clean:
	$(RM) $(TARGET) sh4der-image-test.elf sh4der-warp-test.elf sh4der-ca-shafts-test.elf
