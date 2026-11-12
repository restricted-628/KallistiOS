CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-render-test/include -I../../kernel/arch/dreamcast/include
CPPFLAGS += -I../../addons/libsh4der/upstream/include
CPPFLAGS += -I../../addons/libsh4der/upstream/sh4dow-dc/include
TARGET = sh4der-compact-test
SOURCES = test.c \
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
$(TARGET): $(SOURCES) ../../examples/dreamcast/pvr/sh4der_compact/effects.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	$(RM) $(TARGET)
include ../Makefile.sh4zam-state
