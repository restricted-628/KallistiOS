# KallistiOS ##version##
TARGET = fiber-disc-contract.elf
OBJS = fiber-disc-contract.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -lfiber_disc \
	    -Wl,--wrap=gdrom_direct_read_sectors_dma_async \
	    -Wl,--wrap=gdrom_direct_read_sectors_dma_gaps_async
clean:
	-rm -f $(TARGET) $(OBJS)
