CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-deform-test/include \
	-I../pvr-model-convert \
	-I../../kernel/arch/dreamcast/include
LDLIBS += -lm
CPPFLAGS += -isystem ../../addons/include

TARGET = pvr-chunk-skin-test
SEAM_TARGET = pvr-chunk-joint-seam-test
SEAM_SOURCE = ../../examples/dreamcast/pvr/chunk_joint_seam/chunk-joint-seam.c
SEAM_SOURCES = $(SEAM_SOURCE) \
	$(filter %/pvr_chunk_model.c %/pvr_chunk_skin.c %/pvr_deform.c \
	         %/pvr_lighting.c %/matrix_compose.c,$(SOURCES))
SOURCES = pvr-chunk-skin-test.c \
	../pvr-model-convert/pvr-scene-ir.c \
	../pvr-model-convert/pvr-skeleton-ir.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_volume_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skeleton_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_shape_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_animation_asset.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_deform.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_lighting.c \
	../../kernel/arch/dreamcast/math/matrix_compose.c

.PHONY: all clean test

all: $(TARGET) $(SEAM_TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

$(SEAM_TARGET): $(SEAM_SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DCHUNK_JOINT_SEAM_HOST $(SEAM_SOURCES) $(LDLIBS) -o $@

test: $(TARGET) $(SEAM_TARGET)
	./$(TARGET)
	./$(SEAM_TARGET)

clean:
	rm -f $(TARGET) $(SEAM_TARGET)
	rm -rf $(SEAM_TARGET).dSYM
