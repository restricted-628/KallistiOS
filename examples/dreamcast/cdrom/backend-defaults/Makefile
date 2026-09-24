# KallistiOS ##version##
TARGET = backend-defaults.elf
OBJS = backend-defaults.o

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -Wl,--wrap=gdrom_direct_stream_session_start \
	    -Wl,--wrap=cdrom_stream_session_start_internal \
	    -Wl,--wrap=gdrom_direct_probe,--wrap=gdrom_direct_read_toc \
	    -Wl,--wrap=cdrom_bios_reinit,--wrap=cdrom_bios_get_status \
	    -Wl,--wrap=cdrom_bios_read_toc,--wrap=cdrom_media_recognize

clean:
	-rm -f $(TARGET) $(OBJS)
