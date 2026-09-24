# KallistiOS ##version##
TARGET = spu-lifetime.elf
OBJS = main.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -Wl,--wrap=g2_dma_transfer,--wrap=g2_dma_get_status,--wrap=g2_dma_cancel
clean:
	$(RM) $(TARGET) $(OBJS)
