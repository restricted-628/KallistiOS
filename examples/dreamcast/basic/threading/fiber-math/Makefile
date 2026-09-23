# Core XMTRX preservation probe; no service executor or SH4ZAM dependency.
TARGET = fiber-math.elf
OBJS = fiber-math.o

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS)
.PHONY: all clean run
clean:
	$(RM) $(TARGET) $(OBJS)
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
