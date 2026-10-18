TARGET = resident-fault.elf
OBJS = resident-fault.o fault-state.o
KOS_CFLAGS += -Wextra -Werror
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -Wl,-Map,$@.map -o $@ $(OBJS)
clean:
	-rm -f $(OBJS) $(TARGET) $(TARGET).map
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
