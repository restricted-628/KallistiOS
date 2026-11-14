CC = cc
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic -I../../kernel/arch/dreamcast/include/dc/sound
.PHONY: test sanitize
startup.inc: extract.py ../../kernel/arch/dreamcast/sound/snd_iface.c
	python3 extract.py
startup-test: test.c startup.inc
	$(CC) $(CFLAGS) test.c -o $@
startup-sanitize: test.c startup.inc
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer test.c -o $@
test: startup-test
	./startup-test
sanitize: startup-sanitize
	./startup-sanitize
