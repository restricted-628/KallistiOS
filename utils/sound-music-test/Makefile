CC = cc
ARM = ../../kernel/arch/dreamcast/sound/arm
CPPFLAGS = -I$(ARM) -I../../kernel/arch/dreamcast/include -include ../sound-observation-test/stubs/types.h
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -pedantic
.PHONY: test sanitize
test: music-test bank-test
	./music-test
	./bank-test
sanitize: music-sanitize bank-sanitize
	./music-sanitize
	./bank-sanitize
music-test: test.c $(ARM)/aica_music.c $(ARM)/aica_music.h $(ARM)/aica_music_pitch.inc
	$(CC) $(CPPFLAGS) $(CFLAGS) test.c $(ARM)/aica_music.c -lm -o $@
music-sanitize: test.c $(ARM)/aica_music.c $(ARM)/aica_music.h $(ARM)/aica_music_pitch.inc
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer test.c $(ARM)/aica_music.c -lm -o $@

BANK_SOURCES = bank.c $(ARM)/aica_music.c $(ARM)/aica_music_bank.c $(ARM)/aica_bank_registry.c
BANK_HEADERS = $(ARM)/aica_music_bank.h $(ARM)/aica_music.h $(ARM)/aica_bank_registry.h
bank-test: $(BANK_SOURCES) $(BANK_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BANK_SOURCES) -o $@
bank-sanitize: $(BANK_SOURCES) $(BANK_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(BANK_SOURCES) -o $@
