CC ?= cc
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(EXTRA_CFLAGS)
CPPFLAGS := -I../../kernel/arch/dreamcast/include/dc/sound -Istubs -I../../kernel/arch/dreamcast/include
.PHONY: test clean
exact.inc: extract.py ../../kernel/arch/dreamcast/sound/snd_iface.c
	python3 extract.py
status-test: test.c exact.inc
	$(CC) $(CFLAGS) $(CPPFLAGS) test.c -o $@
test: status-test
	./status-test
clean:
	rm -f status-test exact.inc extraction.json
