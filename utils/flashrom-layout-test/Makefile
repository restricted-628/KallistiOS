# KallistiOS ##version##
#
# Host-side flash record layout tests.

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
ROOT := ../..
INCLUDES := -Iinclude -I$(ROOT)/kernel/arch/dreamcast/include \
            -I$(ROOT)/kernel/arch/dreamcast/hardware
TARGETS := flashrom-layout-test flashrom-mutation-test
LAYOUT_SOURCE := $(ROOT)/kernel/arch/dreamcast/hardware/flashrom_layout.c
MUTATION_SOURCE := $(ROOT)/kernel/arch/dreamcast/hardware/flashrom.c

.PHONY: all clean test

all: $(TARGETS)

flashrom-layout-test: flashrom-layout-test.c $(LAYOUT_SOURCE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

flashrom-mutation-test: flashrom-mutation-test.c $(LAYOUT_SOURCE) \
                        $(MUTATION_SOURCE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test: $(TARGETS)
	./flashrom-layout-test
	./flashrom-mutation-test

clean:
	-rm -f $(TARGETS)
