TARGET = fiber-vblank.elf
OBJS = fiber-vblank.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -Wl,-Map,$@.map -o $@ $^ -lfiber_vblank
clean:
	$(RM) $(TARGET) $(TARGET).map $(OBJS)
