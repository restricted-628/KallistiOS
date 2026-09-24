CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
override CFLAGS += -fno-fast-math -fno-lto
CPPFLAGS += -I../pvr-deform-test/include -I../../kernel/arch/dreamcast/include
CPPFLAGS += -isystem ../../addons/include
LDLIBS += -lm
TARGET = skin-bench-test
SOURCES = ../../examples/dreamcast/sh4zam/integration/skin-bench.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_deform.c
.PHONY: all test clean
all: $(TARGET)
$(TARGET): $(SOURCES) ../../kernel/arch/dreamcast/include/dc/pvr_skin_prepared.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	rm -f $(TARGET)
