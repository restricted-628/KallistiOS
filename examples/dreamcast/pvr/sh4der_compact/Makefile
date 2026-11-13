TARGET = sh4der-compact.elf
OBJS = main.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
main.o: $(KOS_BASE)/addons/include/sh4der_kos_screen.h $(KOS_BASE)/addons/include/sh4der_kos_geometry.h effects.h $(KOS_BASE)/addons/include/sh4der_kos.h
$(TARGET): $(OBJS) $(KOS_BASE)/addons/lib/$(KOS_ARCH)/libsh4der.a
	kos-cc -o $@ $(OBJS) -lsh4der
clean:
	-rm -f $(TARGET) $(OBJS)
