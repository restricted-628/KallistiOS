CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-deform-test/include \
	-I../pvr-model-convert \
	-I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-chunk-shape-test
DREAMCAST_TARGET = pvr-chunk-shape-test.elf
SOURCES = pvr-chunk-shape-test.c \
	../pvr-model-convert/pvr-scene-ir.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_shape.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_shape_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_deform.c

.PHONY: all clean dreamcast test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

dreamcast: $(DREAMCAST_TARGET)

$(DREAMCAST_TARGET): pvr-chunk-shape-test.c \
	../pvr-model-convert/pvr-scene-ir.c
	kos-cc $(KOS_CSTD) -Wall -Wextra -I../pvr-model-convert $^ -lm -o $@

clean:
	rm -f $(TARGET) $(DREAMCAST_TARGET)
	rm -rf $(TARGET).dSYM
