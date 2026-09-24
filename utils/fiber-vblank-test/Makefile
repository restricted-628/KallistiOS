CC ?= cc
CFLAGS ?= -O2
include ../Makefile.host-test
INCLUDES = -Iinclude -I../vblank-handler-test/include -I../../addons/include \
           -I../../kernel/arch/dreamcast/include

.PHONY: all test clean
all: test
test: fiber-vblank-test
	./fiber-vblank-test
fiber-vblank-test: fiber-vblank-test.o vblank.o
	$(CC) $(CFLAGS) -o $@ $^
fiber-vblank-test.o: fiber-vblank-test.c ../../addons/libfiber_vblank/fiber_vblank.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
vblank.o: ../../kernel/arch/dreamcast/hardware/vblank.c
	$(CC) $(CFLAGS) $(INCLUDES) -Dmalloc=test_malloc -Dfree=test_free -c -o $@ $<
clean:
	$(RM) fiber-vblank-test *.o
