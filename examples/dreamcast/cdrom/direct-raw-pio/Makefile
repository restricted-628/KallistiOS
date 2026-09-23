# KallistiOS ##version##
TARGET = direct-raw-pio.elf
OBJS = direct-raw-pio.o gdrom_direct_probe.o
DRIVER = ../../../../kernel/arch/dreamcast/hardware/gdrom_direct.c
WRAPPED = g1_bus_lock_timed g1_bus_unlock g1_bus_select_device_timed g1_bus_wait_status

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

gdrom_direct_probe.o: $(DRIVER) mmio-shim.h
	kos-cc $(CFLAGS) -include mmio-shim.h -c $(DRIVER) -o $@

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) $(foreach symbol,$(WRAPPED),-Wl,--wrap=$(symbol))

clean:
	-rm -f $(TARGET) $(OBJS)
