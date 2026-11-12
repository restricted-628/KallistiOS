# KallistiOS ##version##
# Caller-owned SH4DER-PVR computations; no upstream PVR lifecycle code.
TARGET = libsh4der.a
.DEFAULT_GOAL := defaultall

OBJS = .build/sh4der_core.o .build/sh4der_blade.o \
       .build/sh4der_holo.o .build/sh4der_bloom.o \
       .build/sh4dow_dc.o .build/sh4dow_foot.o

ifeq ($(wildcard upstream/include/sh4der_core.h),)
ifneq ($(MAKECMDGOALS),clean)
$(error SH4DER-PVR is missing. Run git submodule update --init --recursive)
endif
endif

KOS_CFLAGS += -Iupstream/include -Iupstream/sh4dow-dc/include
KOS_CFLAGS += -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT

$(OBJS): $(wildcard upstream/include/*.h upstream/sh4dow-dc/include/*.h)

.build/%.o: upstream/src/%.c
	mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/%.o: upstream/sh4dow-dc/src/%.c
	mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

LOCAL_CLEAN = $(OBJS:.o=.d)
-include $(OBJS:.o=.d)
include $(KOS_BASE)/addons/Makefile.prefab
