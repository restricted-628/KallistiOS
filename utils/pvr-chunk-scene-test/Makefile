CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-binding-test/include \
            -I../pvr-chunk-render-test/include \
            -I../pvr-chunk-model-test/include \
            -I../pvr-model-convert \
            -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-chunk-scene-test
SOURCES = pvr-chunk-scene-test.c \
          ../pvr-model-convert/pvr-scene-ir.c \
          ../pvr-model-convert/pvr-model-table-ir.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model_table.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_volume_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_layer_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_shape_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_animation_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_morph_animation_asset.c

.PHONY: all clean test dreamcast

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

dreamcast: pvr-chunk-scene-test.elf

pvr-chunk-scene-test.elf: pvr-chunk-scene-test.c \
                        ../pvr-model-convert/pvr-scene-ir.c \
                        ../pvr-model-convert/pvr-model-table-ir.c \
                        $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc $(KOS_CSTD) -Wall -Wextra -Werror -I../pvr-model-convert \
		pvr-chunk-scene-test.c ../pvr-model-convert/pvr-scene-ir.c \
		../pvr-model-convert/pvr-model-table-ir.c -lm -o $@

clean:
	rm -f $(TARGET) pvr-chunk-scene-test.elf
	rm -rf $(TARGET).dSYM
