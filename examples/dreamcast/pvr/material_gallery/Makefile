TARGET = material-gallery.elf
OBJS = gallery.o
.PHONY: all clean smoke run
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
gallery.o: gallery.c ../showcase_common.h
	kos-cc $(KOS_CFLAGS) -Wextra -Werror -c $< -o $@
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS)
smoke: material-gallery-smoke.elf
gallery-smoke.o: gallery.c ../showcase_common.h
	kos-cc $(KOS_CFLAGS) -Wextra -Werror -DSHOWCASE_SMOKE -c $< -o $@
material-gallery-smoke.elf: gallery-smoke.o $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $<
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
clean:
	-rm -f $(TARGET) $(OBJS) gallery-smoke.o material-gallery-smoke.elf
