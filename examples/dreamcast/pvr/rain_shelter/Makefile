TARGET = rain-shelter.elf
OBJS = main.o rain.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

main.o rain.o: rain.h
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS) -lm

HOST_CC ?= cc
HOST_FLAGS ?= -O2
PARTICLE = $(KOS_BASE)/kernel/arch/dreamcast/hardware/pvr/pvr_particle.c
ifeq ($(shell uname -s),Darwin)
HOST_GC_SECTIONS = -Wl,-dead_strip
else
HOST_GC_SECTIONS = -Wl,--gc-sections
endif
.PHONY: host-test
host-test: rain-test
	./rain-test
rain-test: rain.c test.c rain.h $(PARTICLE)
	$(HOST_CC) $(HOST_FLAGS) -std=gnu17 -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections $(HOST_GC_SECTIONS) \
	    -DSHZ_TLS_MODEL=SHZ_TLS_PTHREAD -isystem $(KOS_BASE)/addons/include \
	    -I$(KOS_BASE)/utils/pvr-geometry-test/include -I$(KOS_BASE)/kernel/arch/dreamcast/include \
	    test.c rain.c $(PARTICLE) -lm -o $@

clean:
	$(RM) $(TARGET) $(OBJS) rain-test
