CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS += -Iinclude -I../pvr-chunk-render-test/include \
	-I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-chunk-cache-test
DREAMCAST_TARGET = pvr-chunk-cache-test.elf
SOURCES = pvr-chunk-cache-test.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_render.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_cache.c \
	../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c

.PHONY: all clean dreamcast test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

dreamcast: $(DREAMCAST_TARGET)

$(DREAMCAST_TARGET): pvr-chunk-cache-test.c
	kos-cc -std=gnu17 -Wall -Wextra -Werror $< -lm -o $@

clean:
	rm -f $(TARGET) $(DREAMCAST_TARGET)
