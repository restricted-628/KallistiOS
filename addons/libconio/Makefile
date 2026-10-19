# KallistiOS ##version##
#
# libconio Makefile
# Copyright (C) 2002 Megan Potter

TARGET = libconio.a
.DEFAULT_GOAL := defaultall
OBJS =  conio.o input.o draw.o
KOS_CFLAGS += $(KOS_CSTD) -Iinclude -DBUILD_LIBCONIO -MMD -MP
LOCAL_CLEAN = $(OBJS:.o=.d)
-include $(OBJS:.o=.d)

include $(KOS_BASE)/addons/Makefile.prefab
