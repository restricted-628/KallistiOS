# KallistiOS ##version##
#
# basic/threading/fiber-context-probe/Makefile
# Copyright (C) 2026 Joseph Black
#

TARGET = fiber-context-probe.elf
RUNTIME_TARGET = fiber-runtime-probe.elf
OBJS = fiber-context-probe.o fiber-context-switch.o
KOS_CFLAGS += -std=c11

all: $(TARGET) $(RUNTIME_TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS) fiber-runtime-probe.o

rm-elf:
	-rm -f $(TARGET) $(RUNTIME_TARGET) *.elf.map

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

$(RUNTIME_TARGET): fiber-runtime-probe.o
	kos-cc -Wl,-Map,$@.map -o $@ $^

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

run-runtime: $(RUNTIME_TARGET)
	$(KOS_LOADER) $(RUNTIME_TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
