CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-render-test/include -I../../kernel/arch/dreamcast/include
CPPFLAGS += -I../../addons/libsh4der/upstream/include
TARGET = sh4der-compact-test
SOURCES = ../../addons/libsh4der/kos/geometry.c ../../addons/libsh4der/upstream/src/sh4dow_foot.c ../../addons/libsh4der/upstream/src/sh4dow_dc.c test.c ../../addons/libsh4der/kos/compact.c \
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
$(TARGET): ../../addons/include/sh4der_kos_uvwarp.h ../../addons/include/sh4der/sh4der_uvwarp.h
$(TARGET): $(SOURCES) ../../addons/include/sh4der_kos.h ../../addons/include/sh4der_kos_geometry.h ../../examples/dreamcast/pvr/sh4der_compact/effects.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@
test: $(TARGET) routed-test
	./$(TARGET)
	./$(TARGET) --cook-foot > cooked-foot.actual.h
	cmp cooked-foot.h cooked-foot.actual.h
.PHONY: routed-test
routed-test: routed-lighting-test
	./routed-lighting-test

ROUTED_SOURCES = routed-lighting-test.c ../../addons/libsh4der/kos/routed_lighting.c
routed-lighting-test: $(ROUTED_SOURCES) ../../addons/include/sh4der_kos_routed_lighting.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(ROUTED_SOURCES) -lm -o $@

dreamcast:
	kos-cc routed-lighting-test.c -lsh4der -lm -o routed-lighting-test.elf
	kos-cc native.c -lsh4der -o sh4der-compact-test.elf
	kos-cc foot-native.c -lsh4der -o sh4der-foot-test.elf
clean:
	$(RM) $(TARGET) routed-lighting-test routed-lighting-test.elf cooked-foot.actual.h sh4der-compact-test.elf sh4der-foot-test.elf
include ../Makefile.sh4zam-state
