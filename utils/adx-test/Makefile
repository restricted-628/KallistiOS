# Synthetic tests only; no SDK or commercial media required.
CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
ADX = ../../kernel/arch/dreamcast/sound/snd_adx.c
ADX_INCLUDE = ../../kernel/arch/dreamcast/include
HEADER = $(ADX_INCLUDE)/dc/sound/adx.h

.PHONY: all test clean
all: test
adx-test: adx-test.c $(ADX) $(HEADER)
	$(CC) $(CFLAGS) -I$(ADX_INCLUDE) adx-test.c $(ADX) -lm -o $@
adx-decode: adx-decode.c $(ADX) $(HEADER)
	$(CC) $(CFLAGS) -I$(ADX_INCLUDE) adx-decode.c $(ADX) -lm -o $@
test: adx-test
	./adx-test
clean:
	$(RM) adx-test adx-decode
