CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
override CFLAGS += -fno-fast-math -fno-lto
CPPFLAGS += -I../animation-test/include -I../../kernel/arch/dreamcast/include
CPPFLAGS += -isystem ../../addons/include
LDLIBS += -lm
TARGET = matrix-compose-test
EXAMPLE = ../../examples/dreamcast/sh4zam/integration
SOURCES = $(EXAMPLE)/compose-bench.c $(EXAMPLE)/compose-candidate.c \
          ../../kernel/arch/dreamcast/math/matrix_compose.c
SW_SOURCE = ../../addons/libsh4zam/upstream/source/sw/shz_xmtrx_sw.c
# The pinned upstream source lacks a final newline. Keep this formatting-only
# Clang exception on that object, without changing or copying upstream code.
ifneq (,$(findstring clang,$(shell $(CC) --version)))
SW_WARNINGS = -Wno-newline-eof
endif
.PHONY: all test clean
all: $(TARGET)
shz_xmtrx_sw.o: $(SW_SOURCE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SW_WARNINGS) -c $< -o $@
$(TARGET): $(SOURCES) $(EXAMPLE)/compose-candidate.h shz_xmtrx_sw.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) shz_xmtrx_sw.o $(LDLIBS) -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	rm -f $(TARGET) shz_xmtrx_sw.o
