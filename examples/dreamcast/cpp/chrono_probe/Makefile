# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

TARGET = chrono-probe.elf
OBJS = chrono-probe.o
KOS_CPPFLAGS += -std=gnu++23

.PHONY: all clean run dist
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-c++ -o $@ $(OBJS)
clean:
	$(RM) $(TARGET) $(OBJS)
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
dist: $(TARGET)
	$(RM) $(OBJS)
	$(KOS_STRIP) $(TARGET)
