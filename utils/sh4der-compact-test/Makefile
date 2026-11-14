CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-render-test/include -I../../kernel/arch/dreamcast/include
CPPFLAGS += -I../../addons/libsh4der/upstream/include
CPPFLAGS += -I../../addons/libsh4der/upstream/sh4dow-dc/include
TARGET = sh4der-compact-test
SOURCES = ../../addons/libsh4der/kos/geometry.c ../../addons/libsh4der/upstream/sh4dow-dc/src/sh4dow_foot.c ../../addons/libsh4der/upstream/sh4dow-dc/src/sh4dow_dc.c test.c ../../addons/libsh4der/kos/compact.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_route.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_render.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c \
          ../../addons/libsh4der/upstream/src/sh4der_core.c \
          ../../addons/libsh4der/upstream/src/sh4der_blade.c \
          ../../addons/libsh4der/upstream/src/sh4der_bloom.c \
          ../../addons/libsh4der/upstream/src/sh4der_holo.c
all: $(TARGET)
$(TARGET): $(SOURCES) ../../addons/include/sh4der_kos.h ../../addons/include/sh4der_kos_geometry.h ../../examples/dreamcast/pvr/sh4der_compact/effects.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@
test: $(TARGET)
	./$(TARGET)
	./$(TARGET) --cook-foot > cooked-foot.actual.h
	cmp cooked-foot.h cooked-foot.actual.h
dreamcast:
	kos-cc native.c -lsh4der -o sh4der-compact-test.elf
	kos-cc foot-native.c -lsh4der -o sh4der-foot-test.elf
clean:
	$(RM) $(TARGET) cooked-foot.actual.h sh4der-compact-test.elf sh4der-foot-test.elf
include ../Makefile.sh4zam-state
