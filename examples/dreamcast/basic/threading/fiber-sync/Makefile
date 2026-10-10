# KallistiOS ##version##
#
# basic/threading/fiber-sync/Makefile
# Copyright (C) 2026 Joseph Black
#

TARGET = fiber-sync.elf
OBJS = fiber-sync.o ownership.o
KOS_CFLAGS += -std=c11

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS) $(TARGET).map

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -Wl,-Map,$(TARGET).map -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
