# Host regression of the production driver; no target libraries or hardware.
CC = cc
ROOT := ../..
CPPFLAGS += -Iinclude -I$(ROOT)/kernel/arch/dreamcast/fs \
            -idirafter $(ROOT)/include -idirafter $(ROOT)/kernel/arch/dreamcast/include
CFLAGS ?= -O0 -g
CFLAGS += -std=gnu17 -finstrument-functions -Wall -Wextra -Werror -Wno-cpp -pthread
ifneq ($(ISO_SOURCE),)
CPPFLAGS += -DISO_SOURCE='"$(ISO_SOURCE)"'
endif
.PHONY: all check clean
all: iso9660-lock-test
iso9660-lock-test: iso9660-lock-test.c platform.h include/kos/mutex.h \
                   include/kos/thread.h include/kos/sem.h \
                   $(ROOT)/kernel/arch/dreamcast/fs/fs_iso9660.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
check: all
	python3 run.py ./iso9660-lock-test
clean:
	rm -f iso9660-lock-test
