CC = cc
ARM = ../../kernel/arch/dreamcast/sound/arm
CPPFLAGS = -I$(ARM) -I../../kernel/arch/dreamcast/include -include ../sound-observation-test/stubs/types.h
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -pedantic
SOURCES = test.c $(ARM)/aica_music_wire.c $(ARM)/aica_music.c
HEADERS = $(ARM)/aica_music_wire.h $(ARM)/aica_music.h
.PHONY: test sanitize
test: wire-test
	./wire-test
sanitize: wire-sanitize
	./wire-sanitize
wire-test: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@
wire-sanitize: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $@
