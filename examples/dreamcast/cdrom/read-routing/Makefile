# KallistiOS ##version##
TARGET = read-routing.elf
OBJS = read-routing.o
WRAPPED = gdrom_direct_probe gdrom_direct_reinitialize gdrom_direct_read_sectors \
    gdrom_direct_read_sectors_dma gdrom_direct_read_sectors_dma_async \
    g1_bus_lock g1_bus_unlock syscall_gdrom_sector_mode syscall_gdrom_send_command

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) $(foreach symbol,$(WRAPPED),-Wl,--wrap=$(symbol))

clean:
	-rm -f $(TARGET) $(OBJS)
