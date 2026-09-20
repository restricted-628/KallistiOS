CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include
CPPFLAGS += -isystem ../../addons/include
LDLIBS += -lm

TARGET = pvr-deform-test
SOURCES = pvr-deform-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_deform.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES) skin-fixtures.h \
          ../../kernel/arch/dreamcast/include/dc/pvr_skin_prepared.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
