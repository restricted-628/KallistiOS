TARGET = fiber-vblank-service.elf
OBJS = fiber-vblank-service.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
fiber-vblank-service.o: ../fiber-vblank/fiber-vblank.c
$(TARGET): $(OBJS)
	kos-cc -Wl,-Map,$@.map -o $@ $^ -lfiber_vblank
clean:
	$(RM) $(TARGET) $(TARGET).map $(OBJS)
