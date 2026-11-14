# Opt-in bounded video prototype; not part of aggregate addon builds.
.DEFAULT_GOAL := sh4
SH_CC ?= kos-cc
SH_AR ?= sh-elf-ar
HOST_CC ?= cc
SHZ_ROOT ?= ../../libsh4zam/upstream
BUILD ?= .build
FIXTURES ?= $(BUILD)/fixtures
PREFIX ?= $(KOS_BASE)/addons
SH_CFLAGS ?= -O2
HOST_CFLAGS ?= -O2
STRICT = -std=gnu17 -Wall -Wextra -Werror -fno-fast-math -ffp-contract=off -fno-lto
INCLUDES = -Iinclude -isystem $(SHZ_ROOT)/include
NAMES = decoder demux convert idct
SOURCES = $(addprefix src/,$(addsuffix .c,$(NAMES)))
HEADERS = $(wildcard include/kosdec/*.h src/*.h src/reference/*.h)
SH_OBJS = $(addprefix $(BUILD)/sh4/,$(addsuffix .o,$(NAMES)))
HOST_FLAGS = $(HOST_CFLAGS) $(STRICT) $(INCLUDES) -DSHZ_TLS_MODEL=SHZ_TLS_PTHREAD
SHZ_SOURCE = $(SHZ_ROOT)/source/sw/shz_xmtrx_sw.c
CAPTURE_HOLD_MS ?= 0

.PHONY: sh4 install test sanitize fixture-test fixture-sanitize video-example clean FORCE
sh4: $(BUILD)/sh4/libSFD.a
$(BUILD)/sh4/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(STRICT) $(INCLUDES) -c $< -o $@
$(BUILD)/sh4/libSFD.a: $(SH_OBJS)
	$(SH_AR) rcs $@ $^
install: sh4
	mkdir -p $(PREFIX)/include/kosdec $(PREFIX)/lib/dreamcast
	cp include/kosdec/*.h $(PREFIX)/include/kosdec/
	cp $(BUILD)/sh4/libSFD.a $(PREFIX)/lib/dreamcast/
$(BUILD)/host/shz.o: $(SHZ_SOURCE)
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_FLAGS) -Wno-newline-eof -c $< -o $@
$(BUILD)/host/video: tests/video.c $(SOURCES) $(HEADERS) $(BUILD)/host/shz.o
	$(HOST_CC) $(HOST_FLAGS) -DMEDIA_MPEG_NUMERIC_TEST tests/video.c $(SOURCES) $(BUILD)/host/shz.o -lm -pthread -o $@
$(BUILD)/host/color: tests/video.c src/convert.c $(HEADERS) $(BUILD)/host/shz.o
	$(HOST_CC) $(HOST_FLAGS) -DMEDIA_COLOR_ONLY tests/video.c src/convert.c $(BUILD)/host/shz.o -lm -pthread -o $@
test: $(BUILD)/host/color
	python3 tests/selector.py $(BUILD)/host --cc '$(HOST_CC)'
	$(abspath $(BUILD))/host/color
fixture-test: $(BUILD)/host/video
	$(abspath $(BUILD))/host/video $(FIXTURES)
sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize HOST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
fixture-sanitize:
	$(MAKE) fixture-test BUILD=$(BUILD)/sanitize FIXTURES=$(abspath $(FIXTURES)) HOST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
# Rebuild the example on every request so fixture/config changes cannot reuse it.
video-example: sh4 FORCE
	@test -n "$(KOS_BASE)" && test "$(KOS_BASE)" = "$$(cd ../../.. && pwd)"
	@test -f "$(FIXTURES)/movie_data.h" || { echo 'Prepare a supplied SFD with ../tools/prepare-sfd-test.py first'; exit 1; }
	$(SH_CC) $(SH_CFLAGS) $(STRICT) $(INCLUDES) -I$(FIXTURES) \
	    -DCAPTURE_HOLD_MS=$(CAPTURE_HOLD_MS) -DMEDIA_OIX=1 examples/video/main.c \
	    $(BUILD)/sh4/libSFD.a -lsh4zam -lm -o $(BUILD)/sh4/sfd-video.elf \
	    -Wl,-Map,$(BUILD)/sh4/sfd-video.map
clean:
	rm -rf $(BUILD)
FORCE:
