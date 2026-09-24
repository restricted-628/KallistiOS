CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -DCHUNK_SKIN_HOST -I../pvr-chunk-render-test/include \
            -I../../kernel/arch/dreamcast/include
LDLIBS += -lm
TARGET = pvr-skin4-scene-test
COMPACT_TARGET = pvr-skin4-compact-test
PVR = ../../kernel/arch/dreamcast/hardware/pvr
SOURCES = ../../examples/dreamcast/pvr/chunk_skin/chunk-skin.c \
          $(PVR)/pvr_chunk_model.c $(PVR)/pvr_chunk_skin.c \
          $(PVR)/pvr_deform.c $(PVR)/pvr_lighting.c
.PHONY: all test clean
all: $(TARGET) $(COMPACT_TARGET)
$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@
$(COMPACT_TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DCHUNK_SKIN_COMPACT $(SOURCES) $(LDLIBS) -o $@
test: all
	./$(TARGET)
	./$(COMPACT_TARGET)
clean:
	$(RM) $(TARGET) $(COMPACT_TARGET)
