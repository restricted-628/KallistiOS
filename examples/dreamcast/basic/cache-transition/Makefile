# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
TARGET = cache-transition.elf
OBJS = cache-transition.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS)
clean:
	$(RM) $(OBJS) $(TARGET)
