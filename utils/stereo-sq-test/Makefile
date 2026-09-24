CC ?= cc
CFLAGS ?= -O2
HOST_CSTD ?= gnu17
CFLAGS += -std=$(HOST_CSTD) -Wall -Wextra -Werror $(HOST_PEDANTIC)
SOURCE = ../../kernel/arch/dreamcast/sound/snd_pcm16_sq.c

.PHONY: all test clean
all: test
stereo-sq-test: test.c shim.h $(SOURCE)
	$(CC) $(CFLAGS) -Iinclude -include shim.h test.c $(SOURCE) -o $@
test: stereo-sq-test
	./stereo-sq-test
clean:
	$(RM) stereo-sq-test
