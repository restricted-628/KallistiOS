CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../../kernel/arch/dreamcast/include
LDLIBS += -lm

TARGET = animation-test
MATH_DIR = ../../kernel/arch/dreamcast/math
SOURCES = animation-test.c $(MATH_DIR)/animation.c \
          $(MATH_DIR)/matrix_compose.c $(MATH_DIR)/matrix_camera.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
