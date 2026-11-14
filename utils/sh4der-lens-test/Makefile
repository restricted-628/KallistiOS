CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../pvr-reservation-test/include -I../../kernel/arch/dreamcast/include
TARGET = sh4der-lens-test
SOURCES = test.c ../../addons/libsh4der/kos/image.c ../../addons/libsh4der/kos/overlay.c ../../addons/libsh4der/kos/lens.c ../../addons/libsh4der/kos/focus.c ../../addons/libsh4der/kos/visor.c ../../addons/libsh4der/kos/torch.c ../../addons/libsh4der/kos/torch_cpu.c
all: $(TARGET)
$(TARGET): $(SOURCES) ../../addons/include/sh4der_kos_lens.h ../../addons/include/sh4der_kos_overlay.h ../../addons/include/sh4der_kos_visor.h ../../addons/include/sh4der_kos_torch.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast:
	kos-cc native.c -lsh4der -o sh4der-lens-test.elf
	kos-cc visor-native.c -lsh4der -o sh4der-visor-test.elf
	kos-cc torch-native.c -lsh4der -o sh4der-torch-test.elf
clean:
	$(RM) $(TARGET) sh4der-lens-test.elf sh4der-visor-test.elf sh4der-torch-test.elf
