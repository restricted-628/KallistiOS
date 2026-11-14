CC ?= cc
ROOT := ../..
ARM := $(ROOT)/kernel/arch/dreamcast/sound/arm
ASQ := ../aica-sequence-tool/native
SEQUENCE = $(ASQ)/live.c $(ARM)/aica_music_sequence.c $(ASQ)/asq.c $(ASQ)/catalog.c $(ASQ)/wire.c $(ARM)/aica_sequence.c $(ARM)/aica_sequence_resource.c
CPPFLAGS := -I$(ASQ) -Istubs -I$(ARM) -I$(ROOT)/kernel/arch/dreamcast/include/dc/sound -I$(ROOT)/kernel/arch/dreamcast/include -include stubs/types.h
# Mach-O requires an explicit optional undefined symbol; ELF uses the source
# weak reference directly. This applies only to the standalone host fixture.
ifeq ($(shell uname -s),Darwin)
HOST_OPTIONAL_HOOK := -Wl,-U,_snd_playback_reset
endif
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast $(EXTRA_CFLAGS)
.PHONY: test clean
test: arm-test host-test register-test
	./arm-test
	./host-test
	./register-test
register-test: registers.c $(ARM)/aica.c $(ARM)/aica.h
	$(CC) $(CPPFLAGS) $(CFLAGS) registers.c -o $@
MUSIC = $(ARM)/aica_music.c $(ARM)/aica_music_bank.c $(ARM)/aica_music_wire.c $(ARM)/aica_music_live.c
arm-test: $(SEQUENCE) $(MUSIC) arm.c $(ARM)/main.c $(ARM)/aica_observer.c $(ARM)/aica_bank_registry.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -finstrument-functions arm.c $(ARM)/aica_observer.c $(ARM)/aica_bank_registry.c $(MUSIC) $(SEQUENCE) -o $@
clean:
	rm -f arm-test host-test register-test bridges.inc

bridges.inc: extract.py $(ROOT)/kernel/arch/dreamcast/sound/snd_iface.c
	python3 extract.py
host-test: host.c bridges.inc $(ARM)/aica_observer.c $(ROOT)/kernel/arch/dreamcast/sound/snd_observation.c $(wildcard stubs/kos/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_OPTIONAL_HOOK) -pthread host.c $(ARM)/aica_observer.c -o $@
