CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-geometry-test
SOURCES = pvr-geometry-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_sprite_geometry.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES) ../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum_internal.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

include ../Makefile.sh4zam-state
