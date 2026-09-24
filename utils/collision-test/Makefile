CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = collision-test
SOURCES = collision-test.c ../../kernel/arch/dreamcast/math/collision.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
	rm -rf $(TARGET).dSYM
