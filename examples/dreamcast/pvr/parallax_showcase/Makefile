TARGET = parallax-showcase.elf
OBJS = parallax.o
.PHONY: all clean smoke run
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
parallax.o: parallax.c ../showcase_common.h
	kos-cc $(KOS_CFLAGS) -Wextra -Werror -c $< -o $@
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS)
smoke: parallax-smoke.elf
parallax-smoke.o: parallax.c ../showcase_common.h
	kos-cc $(KOS_CFLAGS) -Wextra -Werror -DSHOWCASE_SMOKE -c $< -o $@
parallax-smoke.elf: parallax-smoke.o $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $<
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
clean:
	-rm -f $(TARGET) $(OBJS) parallax-smoke.o parallax-smoke.elf
