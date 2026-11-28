.DEFAULT_GOAL := sh4
SH_CC ?= kos-cc
SH_AR ?= sh-elf-ar
HOST_CC ?= cc
SH_CFLAGS ?= -O2
HOST_CFLAGS ?= -O2
BUILD ?= .build
PREFIX ?= $(KOS_BASE)/addons
COMMON = -std=gnu17 -Wall -Wextra -Werror -Iinclude
HEADER = include/kosdec/acx.h
SOURCE = src/acx.c

.PHONY: sh4 host install test sanitize clean
sh4: $(BUILD)/sh4/libACX.a
host: $(BUILD)/host/acx

$(BUILD)/sh4/acx.o: $(SOURCE) $(HEADER) Makefile
	@mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(COMMON) -c $< -o $@

$(BUILD)/sh4/libACX.a: $(BUILD)/sh4/acx.o
	$(SH_AR) rcs $@ $^

install: sh4
	mkdir -p $(PREFIX)/include/kosdec $(PREFIX)/lib/dreamcast
	cp $(HEADER) $(PREFIX)/include/kosdec/
	cp $(BUILD)/sh4/libACX.a $(PREFIX)/lib/dreamcast/

$(BUILD)/host/acx: src/cli.c $(SOURCE) $(HEADER) Makefile
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(COMMON) $(SOURCE) $< -o $@

$(BUILD)/host/test: tests/acx.c $(SOURCE) $(HEADER) Makefile
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(COMMON) $(SOURCE) $< -o $@

test: $(BUILD)/host/test $(BUILD)/host/acx
	$(BUILD)/host/test
	python3 tests/cli.py $(BUILD)/host/acx

sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize HOST_CC='$(HOST_CC)' \
	    HOST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'

clean:
	rm -rf $(BUILD)
