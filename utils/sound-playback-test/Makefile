CC ?= cc
ROOT := ../..
OBS := ../sound-observation-test
ARM := $(ROOT)/kernel/arch/dreamcast/sound/arm
CPPFLAGS := -Istubs -I$(OBS)/stubs -I$(ARM) -I$(ROOT)/kernel/arch/dreamcast/include/dc/sound -I$(ROOT)/kernel/arch/dreamcast/include -include $(OBS)/stubs/types.h
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(EXTRA_CFLAGS)
SOURCES := $(wildcard $(ROOT)/kernel/arch/dreamcast/sound/snd_*.[ch]) $(wildcard $(ROOT)/kernel/arch/dreamcast/include/dc/sound/*.h)
.PHONY: test clean bridges
test: playback-test
	./playback-test
bridges:
	$(MAKE) -C $(OBS) bridges.inc
playback-test: test.c completion.inc $(OBS)/host.c $(SOURCES) $(ARM)/aica_observer.c bridges
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread test.c $(ARM)/aica_observer.c -o $@
clean:
	rm -f playback-test
