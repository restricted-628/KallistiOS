CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../pvr-model-convert \
	-I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-chunk-animation-test
DREAMCAST_TARGET = pvr-chunk-animation-test.elf
MATH_DIR = ../../kernel/arch/dreamcast/math
SOURCES = pvr-chunk-animation-test.c \
	../pvr-model-convert/pvr-scene-ir.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_volume_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_animation_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_morph_animation_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model_table.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_shape_asset.c \
	$(MATH_DIR)/animation.c $(MATH_DIR)/matrix_compose.c \
	$(MATH_DIR)/matrix_camera.c

.PHONY: all clean dreamcast test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

dreamcast: $(DREAMCAST_TARGET)

$(DREAMCAST_TARGET): pvr-chunk-animation-test.c \
	../pvr-model-convert/pvr-scene-ir.c
	kos-cc $(KOS_CSTD) -Wall -Wextra -I../pvr-model-convert $^ -lm -o $@

clean:
	rm -f $(TARGET) $(DREAMCAST_TARGET)
	rm -rf $(TARGET).dSYM
