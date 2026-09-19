# KallistiOS ##version##
#
# KOS build adapter for the pinned upstream SH4ZAM submodule
# Copyright (C) 2025, 2026 Falco Girgis and SH4ZAM Contributors
#

TARGET = libsh4zam.a
.DEFAULT_GOAL := defaultall

OBJS = .build/shz_matrix.o \
       .build/shz_quat.o \
       .build/shz_version.o \
       .build/shz_xmtrx.o \
       .build/sh4/shz_complex_sh4.o \
       .build/sh4/shz_xmtrx_sh4.o \
       .build/sh4/shz_mem_sh4.o

ifeq ($(wildcard upstream/include/sh4zam/shz_sh4zam.h),)
ifneq ($(MAKECMDGOALS),clean)
$(error SH4ZAM is missing. Run git submodule update --init --recursive from the KOS root)
endif
endif

SHZ_HEADERS = $(wildcard upstream/include/sh4zam/*.h \
              upstream/include/sh4zam/inline/*.h \
              upstream/include/sh4zam/inline/sh4/*.h)

KOS_CFLAGS += $(KOS_CSTD) -Wextra -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT

# GCC 16.2 cannot allocate the eight-point FFT's explicit memory operands at
# -O0. Keep upstream constraints intact and optimize this one translation unit
# even in a debug build. -g is retained; callers can select e.g. -Og explicitly.
SHZ_FFT_CFLAGS ?= -O2
.build/sh4/shz_complex_sh4.o: override CFLAGS += $(SHZ_FFT_CFLAGS)

$(OBJS): $(SHZ_HEADERS)

# Keep artifacts out of the upstream checkout; no source patching at build time.
.build/%.o: upstream/source/%.c
	mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/%.o: upstream/source/%.s
	mkdir -p $(@D)
	kos-as $< -o $@

LOCAL_CLEAN = $(OBJS:.o=.d)
-include $(OBJS:.o=.d)

include $(KOS_BASE)/addons/Makefile.prefab
