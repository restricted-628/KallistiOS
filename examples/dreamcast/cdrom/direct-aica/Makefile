TARGET = gdrom-direct-aica.elf
OBJS = direct-aica.o
KOS_CFLAGS += -I$(KOS_BASE)/kernel/arch/dreamcast/hardware

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS) $(KOS_BASE)/lib/dreamcast/libkallisti.a
	kos-cc -Wl,-Map,$@.map -o $@ $(OBJS)

clean:
	rm -f $(TARGET) $(TARGET).map $(OBJS)
