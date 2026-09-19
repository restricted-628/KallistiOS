# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

TARGET = material-recipes.elf
OBJS = material-recipes.o
VERIFY_PIXELS ?= 0
AUTOSORT_DIAGNOSTIC ?= 0
LAYERS ?= 0
KOS_CFLAGS += -DRECIPE_VERIFY_PIXELS=$(VERIFY_PIXELS)
KOS_CFLAGS += -DRECIPE_AUTOSORT_DIAGNOSTIC=$(AUTOSORT_DIAGNOSTIC)
KOS_CFLAGS += -DRECIPE_LAYERS=$(LAYERS)
.PHONY: all clean run dist
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS) -lm
clean:
	$(RM) $(TARGET) $(OBJS)
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
dist: $(TARGET)
	$(RM) $(OBJS)
	$(KOS_STRIP) $(TARGET)
