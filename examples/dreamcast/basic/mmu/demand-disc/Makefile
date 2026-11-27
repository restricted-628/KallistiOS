TARGET = demand-disc.elf
ADDON = $(KOS_BASE)/addons/libvmdemand
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
.PHONY: addon
addon:
	$(MAKE) -C $(ADDON)
$(TARGET): demand-disc.o addon
	kos-cc -Wl,-Map,$@.map -o $@ demand-disc.o -Wl,--whole-archive $(ADDON)/.build/libvmdemand.a $(FIBER_ARCHIVE) -Wl,--no-whole-archive -Wl,--undefined=_fiber_attach_ex
demand-disc.bin: $(TARGET)
	$(KOS_OBJCOPY) -O binary $< $@
disc: demand-disc.bin
	python3 ../resident-file/make-disc.py $< "$(DISC_DIR)" --scramble "$(KOS_BASE)/utils/scramble/scramble"
