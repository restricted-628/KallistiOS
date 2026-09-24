# KallistiOS ##version##
TARGET = fiber-read.elf
OBJS = fiber-read.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -lfiber_disc
clean:
	-rm -f $(TARGET) $(OBJS)
