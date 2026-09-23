CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../pvr-chunk-render-test/include \
	-I../../kernel/arch/dreamcast/include -DWIRE_COUNT_PROJECTION
LDLIBS += -lm

TARGET = pvr-chunk-cache-test
DREAMCAST_TARGET = pvr-chunk-cache-test.elf
SOURCES = pvr-chunk-cache-test.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_render.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_cache.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_cache_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_toon.c \
	wire-projection-probe.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_lighting.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_toon.c

.PHONY: all clean dreamcast test

all: $(TARGET)

$(TARGET): $(SOURCES) toon-draw-fixtures.h wire-draw-fixtures.h two-volume-toon-fixtures.h \
           ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_wire.c
$(TARGET): ../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum_internal.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

dreamcast: $(DREAMCAST_TARGET)

$(DREAMCAST_TARGET): pvr-chunk-cache-test.c toon-draw-fixtures.h wire-draw-fixtures.h two-volume-toon-fixtures.h
	kos-cc $(KOS_CSTD) -Wall -Wextra $< -lm -o $@

clean:
	rm -f $(TARGET) $(DREAMCAST_TARGET)
	rm -rf $(TARGET).dSYM

include ../Makefile.sh4zam-state
