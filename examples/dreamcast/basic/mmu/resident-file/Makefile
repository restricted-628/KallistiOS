TARGET = resident-file.elf
OBJS = resident-file.o
KOS_CFLAGS += -Wextra -Werror
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -Wl,-Map,$@.map -o $@ $(OBJS)
resident-file.bin: $(TARGET)
	$(KOS_OBJCOPY) -O binary $< $@

# DISC_DIR must be a new directory. This builder uses macOS hdiutil.
disc: resident-file.bin
	python3 make-disc.py $< "$(DISC_DIR)" --scramble "$(KOS_BASE)/utils/scramble/scramble"

clean:
	-rm -f $(OBJS) $(TARGET) $(TARGET).map resident-file.bin
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
