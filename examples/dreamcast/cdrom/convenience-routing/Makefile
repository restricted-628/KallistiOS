# KallistiOS ##version##
TARGET = convenience-routing.elf
OBJS = convenience-routing.o
WRAPPED = gdrom_direct_seek_async gdrom_direct_cdda_get_status \
          gdrom_direct_cdda_get_status_async cdrom_request_submit_internal \
          g1_bus_lock g1_bus_unlock syscall_gdrom_send_command \
          syscall_gdrom_exec_server syscall_gdrom_check_command \
          gdrom_direct_get_status gdrom_direct_read_toc syscall_gdrom_check_drive \
          gdrom_direct_get_subcode gdrom_direct_cdda_play \
          gdrom_direct_cdda_pause gdrom_direct_cdda_resume gdrom_direct_cdda_stop \
          syscall_gdrom_pio_callback syscall_gdrom_pio_transfer \
          syscall_gdrom_pio_check syscall_gdrom_dma_check syscall_gdrom_dma_transfer

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) $(foreach symbol,$(WRAPPED),-Wl,--wrap=$(symbol))

clean:
	-rm -f $(TARGET) $(OBJS)
