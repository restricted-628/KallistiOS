# KallistiOS ##version##
TARGET = backend-defaults.elf
OBJS = backend-defaults.o

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -Wl,--wrap=gdrom_direct_stream_session_start \
	    -Wl,--wrap=cdrom_stream_session_start_internal

clean:
	-rm -f $(TARGET) $(OBJS)
