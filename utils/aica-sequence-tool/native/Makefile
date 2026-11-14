CC = cc
ARM = ../../../kernel/arch/dreamcast/sound/arm
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -pedantic
SOURCES = test.c asq.c $(ARM)/aica_sequence.c
HEADERS = asq.h $(ARM)/aica_sequence.h
asq-check: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I$(ARM) $(SOURCES) -o $@
asq-sanitize: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I$(ARM) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $@
