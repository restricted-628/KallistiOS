# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-material-test/include \
            -I../../kernel/arch/dreamcast/include
TARGET = pvr-material-recipe-test
SOURCES = pvr-material-recipe-test.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_material.c \
          ../../kernel/arch/dreamcast/hardware/pvr/pvr_material_recipe.c
HEADERS = ../../kernel/arch/dreamcast/include/dc/pvr_material.h \
          ../pvr-material-test/include/dc/pvr.h

.PHONY: all test clean dreamcast
all: $(TARGET)
$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast: pvr-material-recipe-test.elf
pvr-material-recipe-test.elf: pvr-material-recipe-test.c \
                            $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc $(KOS_CSTD) -Wall -Wextra $< -lm -o $@
clean:
	$(RM) $(TARGET) pvr-material-recipe-test.elf
