# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
TARGET = adx-fiber.elf
OBJS = adx-fiber.o
KOS_CFLAGS += -std=gnu17 -Wall -Wextra -Werror

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS)
clean:
	$(RM) $(OBJS) $(TARGET)
.PHONY: all clean
