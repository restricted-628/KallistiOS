# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -DCHUNK_SCENE_HOST -include host-support.h \
            -I../pvr-chunk-render-test/include \
            -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-chunk-scene-integration-test
EXAMPLE = ../../examples/dreamcast/pvr/chunk_scene
PVR = ../../kernel/arch/dreamcast/hardware/pvr
MATH = ../../kernel/arch/dreamcast/math
SOURCES = $(EXAMPLE)/chunk-scene.c host-support.c \
          $(PVR)/pvr_chunk_asset.c $(PVR)/pvr_chunk_model.c \
          $(PVR)/pvr_chunk_model_table.c $(PVR)/pvr_chunk_scene.c \
          $(PVR)/pvr_chunk_hierarchy.c \
          $(PVR)/pvr_chunk_scene_asset.c $(PVR)/pvr_chunk_skin.c \
          $(PVR)/pvr_chunk_skin_asset.c $(PVR)/pvr_chunk_skeleton_asset.c \
          $(PVR)/pvr_chunk_shape.c $(PVR)/pvr_chunk_shape_asset.c \
          $(PVR)/pvr_chunk_animation_asset.c \
          $(PVR)/pvr_chunk_morph_animation_asset.c \
          $(PVR)/pvr_chunk_animation_catalog.c \
          $(PVR)/pvr_chunk_cache.c $(PVR)/pvr_chunk_cache_asset.c \
          $(PVR)/pvr_chunk_render.c $(PVR)/pvr_geometry.c \
          $(PVR)/pvr_frustum.c $(PVR)/pvr_deform.c \
          $(PVR)/pvr_lighting.c $(MATH)/animation.c \
          $(MATH)/matrix_compose.c $(MATH)/matrix_camera.c

.PHONY: all test clean
all: $(TARGET)

$(TARGET): $(SOURCES) host-support.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

fixture.pcm: $(EXAMPLE)/chunk-scene.gltf ../pvr-model-convert/pvr-model-convert
	../pvr-model-convert/pvr-model-convert --emit-asset --section-directory \
		--cooked-cache $< $@

../pvr-model-convert/pvr-model-convert:
	$(MAKE) -C ../pvr-model-convert

test: $(TARGET) fixture.pcm
	$(PYTHON) test.py ./$(TARGET) fixture.pcm

clean:
	rm -f $(TARGET) fixture.pcm
