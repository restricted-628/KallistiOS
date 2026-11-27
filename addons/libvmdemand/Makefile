# Explicit application opt-in: never install over libkallisti.a.
OUT = .build
TARGET = $(OUT)/libvmdemand.a
OBJS = $(addprefix $(OUT)/,vm.o mmu.o fs_iso9660.o vm-demand-return.o)
FLAGS = -DKOS_VM_DP1_PRIVATE -Wextra -Werror -MMD -MP -fstack-usage
all: $(TARGET)
$(OUT):
	mkdir -p $@
$(OUT)/vm.o: $(KOS_BASE)/kernel/mm/vm.c | $(OUT)
	kos-cc $(FLAGS) -c $< -o $@
$(OUT)/mmu.o: $(KOS_BASE)/kernel/arch/dreamcast/kernel/mmu.c | $(OUT)
	kos-cc $(FLAGS) -c $< -o $@
$(OUT)/fs_iso9660.o: $(KOS_BASE)/kernel/arch/dreamcast/fs/fs_iso9660.c | $(OUT)
	kos-cc $(FLAGS) -c $< -o $@
$(OUT)/vm-demand-return.o: $(KOS_BASE)/kernel/mm/vm-demand-return.S | $(OUT)
	kos-cc -c $< -o $@
$(TARGET): $(OBJS)
	$(KOS_AR) rcs $@ $(OBJS)
-include $(OBJS:.o=.d)
.PHONY: all
