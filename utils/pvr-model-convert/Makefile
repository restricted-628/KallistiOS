# KallistiOS ##version##
#
# utils/pvr-model-convert/Makefile
# Copyright (C) 2026 Joseph Black
#

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CFLAGS += $(HOST_LZ4_WARNINGS)
CPPFLAGS += -Iinclude -I../pvr-chunk-render-test/include \
            -I../../kernel/arch/dreamcast/include \
            -I../../addons/include -I../pvrtex
LDLIBS += -lm

TARGET = pvr-model-convert
SOURCES = pvr-model-convert.c pvr-scene-ir.c pvr-skeleton-ir.c \
	  pvr-model-table-ir.c cgltf.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model_table.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_render.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_cache.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_cache_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_resource_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_texture_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_volume_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skeleton_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_shape_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_animation_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_morph_animation_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_animation_catalog.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_lighting.c \
          ../../kernel/arch/dreamcast/math/matrix_compose.c \
          ../../addons/liblz4/pvr_chunk_asset_lz4.c \
          ../../addons/liblz4/lz4.c ../../addons/liblz4/lz4frame.c \
          ../../addons/liblz4/lz4hc.c ../../addons/liblz4/xxhash.c \
          ../pvrtex/stb_image_impl.c

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	$(MAKE) -C ../pvr-model-inspect
	./test.py ./$(TARGET) ../pvr-model-inspect/pvr-model-inspect

install: $(TARGET)
	install -m 755 $(TARGET) $(DC_TOOLS_BASE)/

clean:
	rm -f $(TARGET)
