# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
CPPFLAGS += -I../pvr-model-convert
TARGET = pvr-uv-ir-test
SOURCES = pvr-uv-ir-test.c ../pvr-model-convert/pvr-uv-ir.c
.PHONY: all test clean
all: $(TARGET)
$(TARGET): $(SOURCES) ../pvr-model-convert/pvr-uv-ir.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -lm -o $@
test: $(TARGET)
	./$(TARGET)
clean:
	$(RM) $(TARGET)
