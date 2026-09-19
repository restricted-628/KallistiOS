# KallistiOS ##version##
#
# Bundled SH4ZAM 0.8.0 build
# Copyright (C) 2025, 2026 Falco Girgis and SH4ZAM Contributors
# Copyright (C) 2026 Joseph Black
#

TARGET = libsh4zam.a

OBJS = source/shz_matrix.o \
       source/shz_quat.o \
       source/shz_version.o \
       source/shz_xmtrx.o \
       source/sh4/shz_complex_sh4.o \
       source/sh4/shz_xmtrx_sh4.o \
       source/sh4/shz_mem_sh4.o

KOS_CFLAGS += $(KOS_CSTD) -Wextra -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT

include $(KOS_BASE)/addons/Makefile.prefab
