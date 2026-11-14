CC = cc
ARM = ../../kernel/arch/dreamcast/sound/arm
CPPFLAGS = -I$(ARM) -I../../kernel/arch/dreamcast/include -include ../sound-observation-test/stubs/types.h
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -pedantic
SOURCES = test.c music_client.c $(ARM)/aica_music_wire.c $(ARM)/aica_music.c
HEADERS = music_client.h $(ARM)/aica_music_wire.h $(ARM)/aica_music.h
.PHONY: test sanitize
test: client-test
	./client-test
sanitize: client-sanitize
	./client-sanitize
client-test: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@
client-sanitize: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $@
