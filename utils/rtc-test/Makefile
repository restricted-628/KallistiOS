# KallistiOS ##version##
#
# Host-side RTC conversion and register-transaction tests.

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
ROOT := ../..
TEST_INCLUDES := -Iinclude
CALENDAR_SOURCE := $(ROOT)/kernel/arch/dreamcast/hardware/rtc_calendar.c
REGISTER_SOURCE := $(ROOT)/kernel/arch/dreamcast/hardware/rtc.c
TARGETS := rtc-calendar-test rtc-register-test

.PHONY: all clean test

all: $(TARGETS)

rtc-calendar-test: rtc-calendar-test.c $(CALENDAR_SOURCE)
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -o $@ $^

rtc-register-test: rtc-register-test.c $(REGISTER_SOURCE)
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -o $@ $^

test: $(TARGETS)
	./rtc-calendar-test
	./rtc-register-test

clean:
	-rm -f $(TARGETS)
