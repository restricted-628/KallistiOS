# KallistiOS ##version##
# Caller-owned SH4DER-PVR computations; no upstream PVR lifecycle code.
TARGET = libsh4der.a
.DEFAULT_GOAL := defaultall

OBJS = .build/sh4der_core.o .build/sh4der_blade.o \
       .build/sh4der_holo.o .build/sh4der_bloom.o \
       .build/sh4dow_dc.o .build/sh4dow_foot.o .build/compact.o .build/geometry.o .build/screen.o .build/frame.o .build/glow.o .build/image.o .build/focus.o .build/warp.o .build/ca.o .build/shafts.o .build/overlay.o .build/lens.o .build/visor.o .build/torch.o .build/torch_cpu.o .build/trail.o .build/look.o .build/ao.o

ifeq ($(wildcard upstream/include/sh4der_core.h),)
ifneq ($(MAKECMDGOALS),clean)
$(error SH4DER-PVR is missing. Run git submodule update --init --recursive)
endif
endif

KOS_CFLAGS += -Iupstream/include -Iupstream/sh4dow-dc/include
KOS_CFLAGS += -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT

$(OBJS): $(wildcard upstream/include/*.h upstream/sh4dow-dc/include/*.h)

.build/compact.o: kos/compact.c ../include/sh4der_kos.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/geometry.o: kos/geometry.c ../include/sh4der_kos_geometry.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/screen.o: kos/screen.c ../include/sh4der_kos_screen.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/frame.o: kos/frame.c ../include/sh4der_kos_frame.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/glow.o: kos/glow.c ../include/sh4der_kos_glow.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/image.o: kos/image.c ../include/sh4der_kos_image.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/focus.o: kos/focus.c ../include/sh4der_kos_focus.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/warp.o: kos/warp.c ../include/sh4der_kos_warp.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/ca.o: kos/ca.c ../include/sh4der_kos_ca.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/shafts.o: kos/shafts.c ../include/sh4der_kos_shafts.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/overlay.o: kos/overlay.c ../include/sh4der_kos_overlay.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/lens.o: kos/lens.c ../include/sh4der_kos_lens.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/visor.o: kos/visor.c ../include/sh4der_kos_visor.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/torch.o: kos/torch.c ../include/sh4der_kos_torch.h upstream/src/sh4der_torch.c
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/torch_cpu.o: kos/torch_cpu.c ../include/sh4der_kos_torch.h upstream/src/sh4der_torch.c
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/trail.o: kos/trail.c ../include/sh4der_kos_trail.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/look.o: kos/look.c ../include/sh4der_kos_look.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/ao.o: kos/ao.c ../include/sh4der_kos_ao.h
	@mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/%.o: upstream/src/%.c
	mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.build/%.o: upstream/sh4dow-dc/src/%.c
	mkdir -p $(@D)
	kos-cc $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

LOCAL_CLEAN = $(OBJS:.o=.d)
-include $(OBJS:.o=.d)
include $(KOS_BASE)/addons/Makefile.prefab
