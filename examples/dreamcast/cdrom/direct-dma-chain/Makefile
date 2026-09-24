# KallistiOS ##version##
TARGET = direct-dma-chain.elf
OBJS = direct-dma-chain.o gdrom_direct_probe.o cdrom_request_probe.o
HARDWARE = ../../../../kernel/arch/dreamcast/hardware
WRAPPED = gdrom_direct_read_sectors_dma_request cdrom_media_monitor_report_result

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

gdrom_direct_probe.o: $(HARDWARE)/gdrom_direct.c probe-shim.h
	kos-cc $(CFLAGS) -DCHAIN_ALLOC_SPY -include probe-shim.h -c $< -o $@

cdrom_request_probe.o: $(HARDWARE)/cdrom_request.c probe-shim.h
	kos-cc $(CFLAGS) -include probe-shim.h -c $< -o $@

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) $(foreach symbol,$(WRAPPED),-Wl,--wrap=$(symbol))

clean:
	-rm -f $(TARGET) $(OBJS)
