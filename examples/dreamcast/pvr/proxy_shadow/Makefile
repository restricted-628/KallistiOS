TARGET = proxy-shadow.elf
OBJS = main.o proxy.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

proxy-data.inc: cook-proxy.py
	python3 $< > $@.tmp && mv $@.tmp $@
proxy.o: proxy.c proxy.h proxy-data.inc
main.o: main.c proxy.h
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS) -lm

HOST_CC ?= cc
HOST_FLAGS ?= -O2
.PHONY: host-test
host-test: proxy-test
	./proxy-test
proxy-test: test.c proxy.c proxy.h proxy-data.inc
	$(HOST_CC) $(HOST_FLAGS) -std=gnu17 -Wall -Wextra -Werror -DSHZ_TLS_MODEL=SHZ_TLS_PTHREAD \
	    -isystem $(KOS_BASE)/addons/include test.c proxy.c -lm -o $@
clean:
	$(RM) $(TARGET) $(OBJS) proxy-test proxy-data.inc proxy-data.inc.tmp
