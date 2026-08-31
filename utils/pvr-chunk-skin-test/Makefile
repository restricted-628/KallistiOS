CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-deform-test/include \
	-I../pvr-model-convert \
	-I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-chunk-skin-test
SOURCES = pvr-chunk-skin-test.c \
	../pvr-model-convert/pvr-scene-ir.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_shape_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_animation_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_deform.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
