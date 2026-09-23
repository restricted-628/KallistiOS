# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
TARGET = oix-workspace.elf
OBJS = oix-workspace.o
KOS_CFLAGS += -std=gnu17 -Wall -Wextra -Werror
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS)
clean:
	$(RM) $(OBJS) $(TARGET)
.PHONY: all clean
