# KallistiOS ##version##
TARGET = cd-stream-test.elf
OBJS = cd-stream-test.o

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS)

clean:
	-rm -f $(TARGET) $(OBJS)
