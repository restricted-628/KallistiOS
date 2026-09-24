# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
TARGET = icache-iix.elf
OBJS = icache-iix.o array-probe.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS)
clean:
	$(RM) $(OBJS) $(TARGET)
