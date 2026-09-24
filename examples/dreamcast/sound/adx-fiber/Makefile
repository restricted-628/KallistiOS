# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
TARGET = adx-fiber.elf
OBJS = adx-fiber.o
ADX_OIX ?= 0
ADX_MAPPED ?= 1
KOS_CFLAGS += -std=gnu17 -Wall -Wextra -Werror -DADX_OIX=$(ADX_OIX) -DADX_MAPPED=$(ADX_MAPPED)

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS)
clean:
	$(RM) $(OBJS) $(TARGET)
.PHONY: all clean
