CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Istubs -I../../kernel/arch/dreamcast/include
TARGET = sound-port-test
SOURCES = ../../kernel/arch/dreamcast/sound/snd_port.c ../../kernel/arch/dreamcast/sound/snd_channel.c
HEADERS = $(wildcard ../../kernel/arch/dreamcast/sound/snd_*internal.h) ../../kernel/arch/dreamcast/sound/snd_channel.h ../../kernel/arch/dreamcast/include/dc/sound/port.h
.PHONY: all test clean
all: $(TARGET)
$(TARGET): test.c $(SOURCES) $(HEADERS) stubs/kos/irq.h stubs/dc/sound/sfxmgr.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test.c -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	rm -f $(TARGET)
	rm -rf $(TARGET).dSYM
