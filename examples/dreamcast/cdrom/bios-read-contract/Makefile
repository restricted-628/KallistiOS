# KallistiOS ##version##
TARGET = bios-read-contract.elf
OBJS = bios-read-contract.o
WRAPPED = g1_bus_lock g1_bus_unlock syscall_gdrom_check_drive \
          syscall_gdrom_sector_mode syscall_gdrom_send_command \
          syscall_gdrom_exec_server syscall_gdrom_check_command \
          cdrom_request_submit_dma_chain

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) $(foreach symbol,$(WRAPPED),-Wl,--wrap=$(symbol))

clean:
	-rm -f $(TARGET) $(OBJS)
