.DEFAULT_GOAL := sh4
SH_CC ?= kos-cc
SH_AR ?= sh-elf-ar
HOST_CC ?= cc
SH_CFLAGS ?= -O2
HOST_CFLAGS ?= -O2
BUILD ?= .build
PREFIX ?= $(KOS_BASE)/addons
COMMON = -std=gnu17 -Wall -Wextra -Werror -Iinclude
HEADER = include/kosdec/afs.h
SOURCE = src/afs.c

.PHONY: sh4 host install test sanitize clean
sh4: $(BUILD)/sh4/libAFS.a
host: $(BUILD)/host/afs

$(BUILD)/sh4/afs.o: $(SOURCE) $(HEADER) Makefile
	@mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(COMMON) -c $< -o $@

$(BUILD)/sh4/libAFS.a: $(BUILD)/sh4/afs.o
	$(SH_AR) rcs $@ $^

install: sh4
	mkdir -p $(PREFIX)/include/kosdec $(PREFIX)/lib/dreamcast
	cp $(HEADER) $(PREFIX)/include/kosdec/
	cp $(BUILD)/sh4/libAFS.a $(PREFIX)/lib/dreamcast/

$(BUILD)/host/afs: src/cli.c $(SOURCE) $(HEADER) Makefile
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(COMMON) $(SOURCE) $< -o $@

$(BUILD)/host/test: tests/afs.c $(SOURCE) $(HEADER) Makefile
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(COMMON) $(SOURCE) $< -o $@

$(BUILD)/host/partition-test: tests/partition.c $(SOURCE) $(HEADER) Makefile
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(COMMON) $(SOURCE) $< -o $@

test: $(BUILD)/host/test $(BUILD)/host/partition-test $(BUILD)/host/afs
	$(BUILD)/host/test
	$(BUILD)/host/partition-test
	python3 tests/cli.py $(BUILD)/host/afs

sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize HOST_CC='$(HOST_CC)' \
	    HOST_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'

clean:
	rm -rf $(BUILD)
