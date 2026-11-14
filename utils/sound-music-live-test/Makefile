CC = cc
ARM = ../../kernel/arch/dreamcast/sound/arm
ASQ = ../aica-sequence-tool/native
SEQUENCE = $(ASQ)/live.c $(ARM)/aica_music_sequence.c $(ASQ)/asq.c $(ASQ)/catalog.c $(ASQ)/wire.c $(ARM)/aica_sequence.c $(ARM)/aica_sequence_resource.c
CPPFLAGS = -I$(ASQ) -I../sound-observation-test/stubs -I$(ARM) -I../../kernel/arch/dreamcast/include -I../../kernel/arch/dreamcast/include/dc/sound -include ../sound-observation-test/stubs/types.h
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
SOURCES = $(SEQUENCE) test.c $(ARM)/aica_observer.c $(ARM)/aica_bank_registry.c $(ARM)/aica_music.c $(ARM)/aica_music_bank.c $(ARM)/aica_music_wire.c $(ARM)/aica_music_live.c
.PHONY: test sanitize
test: live-test
	./live-test
sanitize: live-sanitize
	./live-sanitize
live-test: $(SOURCES) $(ARM)/main.c ../sound-observation-test/arm.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -finstrument-functions $(SOURCES) -o $@
live-sanitize: $(SOURCES) $(ARM)/main.c ../sound-observation-test/arm.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -finstrument-functions -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $@
.PHONY: host-test host-sanitize
host-test:
	python3 host.py
host-sanitize:
	python3 host.py --sanitize

$(ASQ)/wire-fixture.h:
	$(MAKE) -C $(ASQ) wire-fixture.h
live-test live-sanitize: $(ASQ)/wire-fixture.h
