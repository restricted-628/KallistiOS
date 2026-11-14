# KallistiOS ##version##
TARGET = spu-lifetime.elf
OBJS = main.o
KOS_CFLAGS += -std=gnu17 -Wall -Wextra -Werror
all: $(TARGET)
.PHONY: host-test
host-test:
	$(PYTHON) host/run.py
PYTHON ?= python3
ifneq ($(MAKECMDGOALS),host-test)
include $(KOS_BASE)/Makefile.rules
endif
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -std=gnu17 -Wall -Wextra -Werror -o $@ $(OBJS) -Wl,-Map,spu-lifetime.map -Wl,--wrap=g2_dma_transfer,--wrap=g2_dma_get_status,--wrap=g2_dma_cancel
clean:
	$(RM) $(TARGET) $(OBJS) spu-lifetime.map
