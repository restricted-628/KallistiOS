CC = cc
ARM = ../../kernel/arch/dreamcast/sound/arm
CPPFLAGS = -I../sound-observation-test/stubs -I$(ARM) -I../../kernel/arch/dreamcast/include -I../../kernel/arch/dreamcast/include/dc/sound -include ../sound-observation-test/stubs/types.h
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
SOURCES = test.c $(ARM)/aica_observer.c $(ARM)/aica_bank_registry.c $(ARM)/aica_music.c $(ARM)/aica_music_bank.c $(ARM)/aica_music_wire.c $(ARM)/aica_music_live.c
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
