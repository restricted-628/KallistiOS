# KallistiOS ##version##
#
# basic/threading/fiber-context-probe/Makefile
# Copyright (C) 2026 Joseph Black
#

TARGET = fiber-context-probe.elf
OBJS = fiber-context-probe.o fiber-context-switch.o
KOS_CFLAGS += -std=c11

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS)

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
