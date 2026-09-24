# KallistiOS ##version##
TARGET = direct-raw-dma.elf
OBJS = direct-raw-dma.o gdrom_direct_probe.o
DRIVER = ../../../../kernel/arch/dreamcast/hardware/gdrom_direct.c
WRAPPED = g1_bus_lock_timed g1_bus_unlock g1_bus_select_device_timed \
    g1_bus_wait_status g1_bus_dma_client_register g1_bus_dma_client_unregister \
    g1_bus_gd_command_client_register g1_bus_gd_command_client_unregister \
    g1_bus_gd_command_client_mask g1_bus_gd_command_client_unmask \
    g1_bus_dma_in_progress g1_bus_dma_disable thd_schedule \
    cdrom_request_submit_executor cdrom_request_event_internal \
    cdrom_request_cancel_requested_internal cdrom_request_update_direct_progress \
    gaps_sram_dma_claim gaps_sram_dma_release gaps_sram_get_info

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

gdrom_direct_probe.o: $(DRIVER) mmio-shim.h
	kos-cc $(CFLAGS) -include mmio-shim.h -c $(DRIVER) -o $@

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) $(foreach symbol,$(WRAPPED),-Wl,--wrap=$(symbol))

clean:
	-rm -f $(TARGET) $(OBJS)
