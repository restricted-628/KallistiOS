.DEFAULT_GOAL := all
HOST_CC ?= cc
HOST_CFLAGS ?= -O2
BUILD ?= .build
SHZ_INCLUDE ?= ../../libsh4zam/upstream/include
STRICT = -std=gnu17 -Wall -Wextra -Werror -fno-fast-math -ffp-contract=off -fno-lto
SOURCES = adxcheck.c ../libADX/src/decoder.c ../libADX/src/adx_math.c ../libADX/src/adx_setup.c
HEADERS = ../libADX/include/kosdec/adx.h ../libADX/src/adx_math.h ../libADX/src/adx_filter.h ../libADX/src/adx_setup_table.h $(SHZ_INCLUDE)/sh4zam/shz_cdefs.h
FLAGS = $(STRICT) -I../libADX/include -isystem $(SHZ_INCLUDE) -DMEDIA_ADX_MATH_HOST_TEST

.PHONY: all test sanitize clean
all: $(BUILD)/adxcheck
$(BUILD)/adxcheck: $(SOURCES) $(HEADERS) Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(FLAGS) $(SOURCES) -lm -o $@
$(BUILD)/sanitize/adxcheck: $(SOURCES) $(HEADERS) Makefile
	mkdir -p $(@D)
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(FLAGS) $(SOURCES) -lm -o $@
test: $(BUILD)/adxcheck
	python3 test_adxcheck.py $(BUILD)/adxcheck
sanitize: $(BUILD)/sanitize/adxcheck
	python3 test_adxcheck.py $(BUILD)/sanitize/adxcheck
clean:
	rm -rf $(BUILD)
