CC = cc
DREAMCAST = ../../kernel/arch/dreamcast
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -pedantic
SOURCES = test.c dsp_build.c $(DREAMCAST)/sound/snd_dsp_program.c
HEADERS = dsp_build.h $(DREAMCAST)/include/dc/sound/dsp.h
INCLUDES = -I../snd-dsp-test/include -I$(DREAMCAST)/include
.PHONY: test sanitize
test: dsp-build-test effects-test
	./dsp-build-test
	./effects-test
sanitize: dsp-build-sanitize effects-sanitize
	./dsp-build-sanitize
	./effects-sanitize
dsp-build-test: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@
dsp-build-sanitize: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $@

EFFECT_SOURCES = test_effects.c dsp_effects.c dsp_build.c $(DREAMCAST)/sound/snd_dsp_program.c
effects-test: $(EFFECT_SOURCES) dsp_effects.h $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(EFFECT_SOURCES) -o $@
effects-sanitize: $(EFFECT_SOURCES) dsp_effects.h $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -fsanitize=address,undefined -fno-omit-frame-pointer $(EFFECT_SOURCES) -o $@
