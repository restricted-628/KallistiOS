# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -Iinclude -I../pvr-chunk-render-test/include \
            -I../../kernel/arch/dreamcast/include
TARGET = irq-stack-test
HEADER = ../../kernel/arch/dreamcast/include/arch/irq.h
.PHONY: all test clean dreamcast
all: $(TARGET) $(TARGET)-plain
$(TARGET): irq-stack-test.c $(HEADER)
	$(CC) $(CFLAGS) $(CPPFLAGS) -D__SH_ATOMIC_MODEL_SOFT_GUSA__=1 $< -o $@
$(TARGET)-plain: irq-stack-test.c $(HEADER)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@
test: all
	./$(TARGET)
	./$(TARGET)-plain
dreamcast: irq-stack-test.elf
irq-stack-test.elf: irq-stack-test.c $(HEADER) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc $(KOS_CSTD) -Wall -Wextra -Werror $< -o $@
clean:
	$(RM) $(TARGET) $(TARGET)-plain irq-stack-test.elf
