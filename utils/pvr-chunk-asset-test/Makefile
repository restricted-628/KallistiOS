CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CFLAGS += $(HOST_LZ4_WARNINGS)
CPPFLAGS += -I../pvr-chunk-model-test/include \
            -I../../kernel/arch/dreamcast/include -I../../addons/include
LDLIBS += -lm

TARGET = pvr-chunk-asset-test
SOURCES = pvr-chunk-asset-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_asset.c \
          ../../addons/liblz4/pvr_chunk_asset_lz4.c \
          ../../addons/liblz4/lz4.c ../../addons/liblz4/lz4frame.c \
          ../../addons/liblz4/lz4hc.c ../../addons/liblz4/xxhash.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
