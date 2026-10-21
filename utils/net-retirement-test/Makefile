CC = clang
CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
include ../Makefile.host-test
TARGETS = driver-test network-test platform-test
.PHONY: test clean
.NOTPARALLEL:
.SECONDARY:

platform-test: hardware.inc scif.inc

test: $(TARGETS)
	./driver-test
	./network-test
	./platform-test
	python3 callers.py
	python3 linkage.py

%.inc: extract.py ../../kernel/arch/dreamcast/hardware/network/broadband_adapter.c ../../kernel/net/net_core.c ../../kernel/arch/dreamcast/kernel/init.c ../../kernel/arch/dreamcast/hardware/hardware.c ../../kernel/arch/dreamcast/hardware/scif.c
	python3 extract.py

%-test: %-test.c %.inc test-shim.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(RM) $(TARGETS) *.inc
	$(RM) -r *.dSYM
