CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CFLAGS += $(HOST_LZ4_WARNINGS)
CPPFLAGS += -Iinclude -I../pvr-chunk-model-test/include \
            -I../../kernel/arch/dreamcast/include -I../../addons/include \
            -include test-alloc.h -DLZ4F_HEAPMODE=1
TARGET = lz4-adapter-test
SOURCES = test.c test-alloc.c \
          ../../addons/liblz4/pvr_chunk_asset_lz4.c \
          ../../addons/liblz4/lz4.c ../../addons/liblz4/lz4frame.c \
          ../../addons/liblz4/lz4hc.c ../../addons/liblz4/xxhash.c
HEADERS = test-alloc.h $(wildcard include/*/*.h) \
          ../../addons/include/kos/pvr_chunk_asset_lz4.h \
          ../../addons/include/kos/pvr_chunk_asset_lz4_service.h \
          ../../addons/liblz4/pvr_chunk_asset_lz4_service.c

.PHONY: all test clean
all: $(TARGET)
$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	rm -f $(TARGET)
