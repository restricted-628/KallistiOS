# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

TARGET = multipass-depth.elf
OBJS = multipass-depth.o
.PHONY: all clean run dist
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS)
clean:
	$(RM) $(TARGET) $(OBJS)
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
dist: $(TARGET)
	$(RM) $(OBJS)
	$(KOS_STRIP) $(TARGET)
