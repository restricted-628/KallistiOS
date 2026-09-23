# KallistiOS ##version##
TARGET = g1-command-ownership.elf
OBJS = g1-command-ownership.o
WRAPPED = g1_bus_lock g1_bus_unlock thd_poll syscall_gdrom_send_command \
          syscall_gdrom_exec_server syscall_gdrom_check_command \
          syscall_gdrom_abort_command syscall_gdrom_reset syscall_gdrom_init

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) $(foreach symbol,$(WRAPPED),-Wl,--wrap=$(symbol))

clean:
	-rm -f $(TARGET) $(OBJS)
