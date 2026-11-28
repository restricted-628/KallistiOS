TARGET = fiber-service-lifecycle.elf
OBJS = fiber-service-lifecycle.o

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	kos-cc -Wl,--wrap=thd_create_ex,--wrap=fiber_create,--wrap=fiber_destroy,--wrap=_fiber_cancel_wait,-Map,$@.map -o $@ $(OBJS)

clean:
	rm -f $(TARGET) $(TARGET).map $(OBJS)
