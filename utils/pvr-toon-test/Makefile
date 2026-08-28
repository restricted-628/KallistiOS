CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = pvr-toon-test
SOURCES = pvr-toon-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_toon.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
