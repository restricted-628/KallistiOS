# KallistiOS ##version##
#
# examples/dreamcast/sound/stream-status/Makefile
#

TARGET = stream-status.elf
OBJS = main.o

.PHONY: all clean run dist

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-$(RM) $(TARGET) $(OBJS)

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	$(RM) $(OBJS)
	$(KOS_STRIP) $(TARGET)
