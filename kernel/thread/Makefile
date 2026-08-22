# KallistiOS ##version##
#
# thread/Makefile
# Copyright (C) 2001 Megan Potter
#

OBJS =  sem.o cond.o mutex.o genwait.o
OBJS += thread.o fiber.o fiber_sync.o fiber_service.o rwsem.o once.o tls.o barrier.o
OBJS += oneshot_timer.o timer_event.o worker.o workqueue.o
SUBDIRS = 

# On toolchains that support the C23 standard (aka. GCC > 14), compile-test
# our gthr-kos.h against the regular KOS includes, to make sure that the
# toolchain file does not lag behind. This is deliberately a phony syntax
# check: compiling a header directly creates a precompiled header, which must
# not be copied into the kernel object directory or archived in libkallisti.
TOOLCHAIN_TEST = $(intcmp $(firstword $(subst ., ,${KOS_GCCVER})),14,,,toolchain_test)

include $(KOS_BASE)/Makefile.prefab

defaultall: $(TOOLCHAIN_TEST)

# Remove artifacts created by older versions of this compile check.
clean: clean_toolchain_test

.PHONY: toolchain_test clean_toolchain_test

toolchain_test:
	kos-cc -DTEST_GTHR_KOS_API -std=c23 -x c -c \
		${KOS_BASE}/utils/kos-chain/patches/gcc/gthr-kos.h -o /dev/null

clean_toolchain_test:
	-rm -f toolchain_test.o toolchain_test.gch
