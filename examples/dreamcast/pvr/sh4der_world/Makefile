TARGET = sh4der-world.elf
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): world.o $(KOS_BASE)/addons/lib/$(KOS_ARCH)/libsh4der.a
	kos-cc -o $@ world.o -lsh4der
world.o: $(KOS_BASE)/addons/include/sh4der_kos_world.h
clean:
	$(RM) $(TARGET) world.o
