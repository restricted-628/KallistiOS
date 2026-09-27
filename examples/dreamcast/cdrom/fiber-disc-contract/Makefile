# KallistiOS ##version##
TARGET = fiber-disc-contract.elf
OBJS = fiber-disc-contract.o stream-contract.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -lfiber_disc \
	    -Wl,--wrap=gdrom_direct_read_sectors_dma_async \
	    -Wl,--wrap=gdrom_direct_read_sectors_dma_gaps_async \
	    -Wl,--wrap=gdrom_direct_stream_begin \
	    -Wl,--wrap=gdrom_direct_stream_transfer \
	    -Wl,--wrap=gdrom_direct_stream_end \
	    -Wl,--wrap=gdrom_direct_stream_session_start \
	    -Wl,--wrap=genwait_wake_all
clean:
	-rm -f $(TARGET) $(OBJS)
