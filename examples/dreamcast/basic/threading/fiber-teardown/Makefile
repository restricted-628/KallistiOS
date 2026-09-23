# Core fiber owner teardown and allocation-accounting probe.
TARGET = fiber-teardown.elf
OBJS = fiber-teardown.o
WRAPS = -Wl,--wrap=calloc,--wrap=aligned_alloc,--wrap=free

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS) $(WRAPS)
.PHONY: all clean run
clean:
	$(RM) $(TARGET) $(OBJS)
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
