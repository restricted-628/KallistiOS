# KallistiOS ##version##
#
# examples/dreamcast/sound/channel-sync/Makefile
#

TARGET = channel-sync.elf
OBJS = main.o

all: clean $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-$(RM) $(TARGET) $(OBJS)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	$(RM) $(OBJS)
	$(KOS_STRIP) $(TARGET)
