TARGET = chunk-routed-lighting.elf
OBJS = main.o
SH4DER = $(KOS_BASE)/addons/lib/$(KOS_ARCH)/libsh4der.a

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

main.o: $(KOS_BASE)/addons/include/sh4der_kos_routed_lighting.h
$(TARGET): $(OBJS) $(SH4DER) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS) -lsh4der -lm

clean:
	rm -f $(TARGET) $(OBJS) cook-receiver receiver-section.inc receiver-section.inc.tmp

HOST_CC ?= cc
HOST_FLAGS ?= -O2
HOST_SHIMS = $(KOS_BASE)/utils/pvr-chunk-render-test/include

.PHONY: host-test
host-test:
	$(MAKE) -C $(KOS_BASE)/utils/sh4der-compact-test routed-test

HOST_PVR = $(KOS_BASE)/kernel/arch/dreamcast/hardware/pvr
HOST_CODEC = $(addprefix $(HOST_PVR)/,pvr_chunk_model.c pvr_chunk_render.c \
             pvr_chunk_cache.c pvr_chunk_cache_asset.c)
# Link only the codec functions reached by the cooker. Submission uses the PVR.
ifeq ($(shell uname -s),Darwin)
HOST_GC_SECTIONS = -Wl,-dead_strip
else
HOST_GC_SECTIONS = -Wl,--gc-sections
endif
cook-receiver: cook-receiver.c $(HOST_CODEC)
	$(HOST_CC) $(HOST_FLAGS) -std=gnu17 -Wall -Wextra -Werror \
	    -DSHZ_TLS_MODEL=SHZ_TLS_PTHREAD -isystem $(KOS_BASE)/addons/libsh4zam/upstream/include \
	    -ffunction-sections -fdata-sections $(HOST_GC_SECTIONS) \
	    -I$(HOST_SHIMS) -I$(KOS_BASE)/kernel/arch/dreamcast/include $^ -lm -o $@

receiver-section.inc: cook-receiver
	./cook-receiver > $@.tmp && mv $@.tmp $@

main.o: receiver-section.inc
