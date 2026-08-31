CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-model-test/include \
            -I../pvr-model-convert \
            -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-chunk-scene-test
SOURCES = pvr-chunk-scene-test.c \
          ../pvr-model-convert/pvr-scene-ir.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin_asset.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
