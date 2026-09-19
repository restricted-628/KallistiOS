CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-geometry-test/include \
            -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-cell-test
DREAMCAST_TARGET = pvr-cell-test.elf
SOURCES = pvr-cell-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_cell.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_cell_asset.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_sprite_geometry.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c

.PHONY: all clean dreamcast test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

dreamcast: $(DREAMCAST_TARGET)

$(DREAMCAST_TARGET): pvr-cell-test.c
	kos-cc $(KOS_CSTD) -Wall -Wextra $< -lm -o $@

clean:
	rm -f $(TARGET) $(DREAMCAST_TARGET)
	rm -rf $(TARGET).dSYM
