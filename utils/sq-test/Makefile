CC ?= cc
CFLAGS ?= -O2
HOST_CSTD ?= gnu17
CFLAGS += -std=$(HOST_CSTD) -Wall -Wextra -Werror $(HOST_PEDANTIC)
ifeq ($(shell uname -s),Darwin)
# Leave the fake 32-bit hardware addresses available to mmap on 64-bit macOS.
LDFLAGS += -Wl,-pagezero_size,0x1000
endif

.PHONY: all test clean
all: test
sq-test: sq-test.c sq-test-shim.h ../../kernel/arch/dreamcast/hardware/sq.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Iinclude -include sq-test-shim.h -o $@ sq-test.c
test: sq-test
	./sq-test
clean:
	$(RM) sq-test
