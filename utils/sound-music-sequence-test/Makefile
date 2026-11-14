CC = cc
ARM = ../../kernel/arch/dreamcast/sound/arm
CPPFLAGS = -I$(ARM) -I../../kernel/arch/dreamcast/include -include ../sound-observation-test/stubs/types.h
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -pedantic
SOURCES = test.c $(ARM)/aica_music_sequence.c $(ARM)/aica_sequence.c $(ARM)/aica_music.c $(ARM)/aica_music_bank.c $(ARM)/aica_bank_registry.c
HEADERS = $(ARM)/aica_music_sequence.h $(ARM)/aica_sequence.h $(ARM)/aica_music.h $(ARM)/aica_music_bank.h $(ARM)/aica_bank_registry.h
.PHONY: test sanitize
test: adapter-test
	./adapter-test
sanitize: adapter-sanitize
	./adapter-sanitize
adapter-test: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@
adapter-sanitize: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $@
