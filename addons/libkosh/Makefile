# libkosh for KallistiOS ##version##
#
# Makefile
# (c)2002 Megan Potter

TARGET = libkosh.a
.DEFAULT_GOAL := defaultall
OBJS = builtin.o chdir.o kosh.o input.o
KOS_CFLAGS += $(KOS_CSTD) -Iinclude -MMD -MP
LOCAL_CLEAN = $(OBJS:.o=.d)
-include $(OBJS:.o=.d)

include $(KOS_BASE)/addons/Makefile.prefab
