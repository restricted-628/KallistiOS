# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
PYTHON ?= python3
ENTRY = ../../kernel/arch/dreamcast/kernel/entry.s
TARGET = irq-return-probe.elf
OBJS = probe.o probe-trap.o entry-probe.o

.PHONY: test dreamcast clean
test:
	$(PYTHON) verify.py

dreamcast: $(TARGET)

entry-probe.s: $(ENTRY) instrument.py
	$(PYTHON) instrument.py $(ENTRY) $@

ifneq ($(KOS_BASE),)
include $(KOS_BASE)/Makefile.rules
endif

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -Wl,-Map,irq-return-probe.map

probe.o: probe.c
	kos-cc $(KOS_CSTD) -Wall -Wextra -Werror -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) irq-return-probe.map entry-probe.s
