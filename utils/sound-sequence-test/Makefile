CC = cc
ARM = ../../kernel/arch/dreamcast/sound/arm
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -pedantic
.PHONY: test sanitize
test: sequence-test
	./sequence-test
sanitize: sequence-sanitize
	./sequence-sanitize
sequence-test: test.c $(ARM)/aica_sequence.c $(ARM)/aica_sequence.h
	$(CC) $(CFLAGS) -I$(ARM) test.c $(ARM)/aica_sequence.c -o $@
sequence-sanitize: test.c $(ARM)/aica_sequence.c $(ARM)/aica_sequence.h
	$(CC) $(CFLAGS) -I$(ARM) -fsanitize=address,undefined -fno-omit-frame-pointer test.c $(ARM)/aica_sequence.c -o $@
