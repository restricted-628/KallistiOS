# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black

CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
SCENE = ../../examples/dreamcast/pvr/portal
PVR = ../../kernel/arch/dreamcast
CPPFLAGS += -I../pvr-geometry-test/include -I$(PVR)/include -I$(SCENE)
TARGET = pvr-portal-test
SOURCES = pvr-portal-test.c $(SCENE)/portal-scene.c \
          $(PVR)/hardware/pvr/pvr_frustum.c
HEADERS = $(SCENE)/portal-scene.h $(PVR)/include/dc/pvr_frustum.h
.PHONY: all test clean
all: test
$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	$(RM) $(TARGET)
