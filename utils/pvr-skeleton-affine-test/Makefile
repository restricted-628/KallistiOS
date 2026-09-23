# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-chunk-render-test/include -I../../kernel/arch/dreamcast/include
TARGET = pvr-skeleton-affine-test
PVR = ../../kernel/arch/dreamcast/hardware/pvr
SOURCES = test.c $(PVR)/pvr_chunk_skeleton_affine.c \
          $(PVR)/pvr_chunk_skeleton_asset.c $(PVR)/pvr_lighting.c \
          $(PVR)/pvr_deform.c ../../kernel/arch/dreamcast/math/matrix_compose.c
HEADERS = ../../kernel/arch/dreamcast/include/dc/pvr_chunk_skeleton_affine.h \
          $(PVR)/pvr_skin_internal.h
.PHONY: all test clean dreamcast
all: $(TARGET)
$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDLIBS) -o $@
test: $(TARGET)
	./$(TARGET)
dreamcast: $(TARGET).elf
$(TARGET).elf: test.c $(HEADERS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -Wall -Wextra -Werror $< -lm -o $@
clean:
	$(RM) $(TARGET) $(TARGET).elf
include ../Makefile.sh4zam-state
