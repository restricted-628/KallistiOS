CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
SOUND = ../../kernel/arch/dreamcast/sound
INCLUDE = ../../kernel/arch/dreamcast/include
SOURCES = $(SOUND)/snd_adx.c $(SOUND)/snd_adx_pipe.c $(SOUND)/snd_adx_input.c
HEADERS = $(INCLUDE)/dc/sound/adx.h $(INCLUDE)/dc/sound/adx_pipe.h $(INCLUDE)/dc/sound/adx_input.h

.PHONY: all test clean
all: test
adx-pipe-test: adx-pipe-test.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I$(INCLUDE) adx-pipe-test.c $(SOURCES) -lm -pthread -o $@
test: adx-pipe-test
	./adx-pipe-test
clean:
	$(RM) adx-pipe-test
