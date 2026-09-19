CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-geometry-test/include \
            -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-particle-test
SOURCES = pvr-particle-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_particle.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_geometry.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(TARGET).dSYM
