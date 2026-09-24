CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
override CFLAGS += -fno-fast-math -fno-lto
CPPFLAGS += -isystem ../../addons/include
LDLIBS += -lm
TARGET = sh4zam-release-test
EXAMPLE = ../../examples/dreamcast/sh4zam/integration
SW_SOURCE = ../../addons/libsh4zam/upstream/source/sw/shz_xmtrx_sw.c
SHZ_HEADERS = $(wildcard ../../addons/include/sh4zam/*.h \
              ../../addons/include/sh4zam/inline/*.h \
              ../../addons/include/sh4zam/inline/sw/*.h)
ifneq (,$(findstring clang,$(shell $(CC) --version)))
SW_WARNINGS = -Wno-newline-eof
endif
.PHONY: all test clean
all: $(TARGET)
shz_xmtrx_sw.o: $(SW_SOURCE) $(SHZ_HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SW_WARNINGS) -c $< -o $@
$(TARGET): test.c $(EXAMPLE)/release-fixtures.h shz_xmtrx_sw.o $(SHZ_HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) test.c shz_xmtrx_sw.o $(LDLIBS) -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	rm -f $(TARGET) shz_xmtrx_sw.o
