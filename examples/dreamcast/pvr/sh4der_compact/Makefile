TARGET = sh4der-compact.elf
OBJS = main.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
main.o: effects.h
$(TARGET): $(OBJS) $(KOS_BASE)/addons/lib/$(KOS_ARCH)/libsh4der.a
	kos-cc -o $@ $(OBJS) -lsh4der
clean:
	-rm -f $(TARGET) $(OBJS)
