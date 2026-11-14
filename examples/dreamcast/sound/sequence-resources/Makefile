TARGET = sequence-resources.elf
ASQ = $(KOS_BASE)/utils/aica-sequence-tool/native
ARM = $(KOS_BASE)/kernel/arch/dreamcast/sound/arm
OBJS = main.o owner.o client.o asq.o aica_sequence.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
KOS_CFLAGS += -I$(ASQ) -I$(ARM) -I$(KOS_BASE)/kernel/arch/dreamcast/sound
score.h: make-score.py $(KOS_BASE)/utils/aica-sequence-tool/asq.py $(KOS_BASE)/utils/aica-sequence-tool/test_asq.py
	python3 make-score.py
main.o: score.h
owner.o: $(ASQ)/owner.c $(ASQ)/owner.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@
client.o: $(ASQ)/client.c $(ASQ)/client.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@
asq.o: $(ASQ)/asq.c $(ASQ)/asq.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@
aica_sequence.o: $(ARM)/aica_sequence.c $(ARM)/aica_sequence.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -Wl,-Map,sequence-resources.map -o $@ $(OBJS)
clean:
	-rm -f $(TARGET) $(OBJS) score.h sequence-resources.map
