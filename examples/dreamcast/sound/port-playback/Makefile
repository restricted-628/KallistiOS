TARGET = port-playback.elf
OBJS = main.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS)
clean:
	-rm -f $(TARGET) $(OBJS)
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
