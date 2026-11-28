.DEFAULT_GOAL := sh4
SH_CC ?= kos-cc
SH_AR ?= sh-elf-ar
HOST_CC ?= cc
SH_CFLAGS ?= -O2
HOST_CFLAGS ?= -O2
BUILD ?= .build
PREFIX ?= $(KOS_BASE)/addons
SHZ_ROOT ?= ../../libsh4zam/upstream
AHX_SYNTH ?= fipr

ifeq ($(AHX_SYNTH),fmac)
SYNTH_FLAGS = -DKOSDEC_AHX_FMAC
else ifneq ($(AHX_SYNTH),fipr)
$(error AHX_SYNTH must be fipr or fmac)
else
SYNTH_INCLUDES = -isystem $(SHZ_ROOT)/include
endif

WARNINGS = -std=gnu17 -Wall -Wextra -Werror -Iinclude
FP_FLAGS = -fno-fast-math -ffp-contract=off -frounding-math -fno-lto
PUBLIC_HEADERS = include/kosdec/ahx.h include/kosdec/ahx_stream.h
HEADERS = $(PUBLIC_HEADERS) src/frame.h src/tables.h
SOURCES = src/parser.c src/decoder.c src/stream.c
MODE_BUILD = $(BUILD)/$(AHX_SYNTH)
SH_OBJECTS = $(patsubst src/%.c,$(MODE_BUILD)/sh4/%.o,$(SOURCES))

.PHONY: sh4 install test sanitize clean
sh4: $(MODE_BUILD)/sh4/libAHX.a

$(MODE_BUILD)/sh4/%.o: src/%.c $(HEADERS) Makefile
	@mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(WARNINGS) $(FP_FLAGS) $(SYNTH_INCLUDES) $(SYNTH_FLAGS) -MD -MP -c $< -o $@

$(MODE_BUILD)/sh4/libAHX.a: $(SH_OBJECTS)
	$(SH_AR) rcs $@ $^

install: sh4
	mkdir -p $(PREFIX)/include/kosdec $(PREFIX)/lib/dreamcast
	cp $(PUBLIC_HEADERS) $(PREFIX)/include/kosdec/
	cp $(MODE_BUILD)/sh4/libAHX.a $(PREFIX)/lib/dreamcast/

$(MODE_BUILD)/host/%: tests/%.c tests/silent.h $(SOURCES) $(HEADERS) Makefile
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(WARNINGS) $(FP_FLAGS) $(SYNTH_FLAGS) $< $(SOURCES) -lm -o $@

test: $(MODE_BUILD)/host/parser $(MODE_BUILD)/host/decoder $(MODE_BUILD)/host/stream
	$(MODE_BUILD)/host/parser
	$(MODE_BUILD)/host/decoder
	$(MODE_BUILD)/host/stream

sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize AHX_SYNTH=$(AHX_SYNTH) HOST_CC='$(HOST_CC)' \
	    HOST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'

clean:
	rm -rf $(BUILD)

-include $(SH_OBJECTS:.o=.d)
