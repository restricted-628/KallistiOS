TARGET = sequence-live.elf
BANK = $(KOS_BASE)/utils/aica-bank-tool/native
CLIENT = $(KOS_BASE)/utils/aica-music-client
ARM = $(KOS_BASE)/kernel/arch/dreamcast/sound/arm
ASQ = $(KOS_BASE)/utils/aica-sequence-tool/native
OBJS = live-owner.o live-client.o owner.o client.o asq.o aica_sequence.o main.o abk.o abk_upload.o abk_registry.o music_client.o music_live.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
KOS_CFLAGS += -I$(ASQ) -I$(BANK) -I$(CLIENT) -I$(ARM) -I$(KOS_BASE)/kernel/arch/dreamcast/sound
bank.h: make-bank.py $(KOS_BASE)/utils/aica-bank-tool/aica_bank.py $(KOS_BASE)/utils/aica-bank-tool/fixture.py
	python3 make-bank.py
main.o: bank.h score.h
abk.o: $(BANK)/abk.c
	kos-cc $(KOS_CFLAGS) -c $< -o $@
abk_upload.o: $(BANK)/abk_upload.c
	kos-cc $(KOS_CFLAGS) -c $< -o $@
abk_registry.o: $(BANK)/abk_registry.c
	kos-cc $(KOS_CFLAGS) -c $< -o $@
music_client.o: $(CLIENT)/music_client.c
	kos-cc $(KOS_CFLAGS) -c $< -o $@
music_live.o: $(CLIENT)/music_live.c
	kos-cc $(KOS_CFLAGS) -c $< -o $@
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -Wl,-Map,sequence-live.map -o $@ $(OBJS)
clean:
	-rm -f $(TARGET) $(OBJS) bank.h score.h sequence-live.map
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

score.h: make-score.py $(KOS_BASE)/utils/aica-sequence-tool/asq.py
	python3 make-score.py

live-owner.o: $(ASQ)/live-owner.c $(ASQ)/live-owner.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@

live-client.o: $(ASQ)/live-client.c $(ASQ)/live-client.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@

owner.o: $(ASQ)/owner.c $(ASQ)/owner.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@

client.o: $(ASQ)/client.c $(ASQ)/client.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@

asq.o: $(ASQ)/asq.c $(ASQ)/asq.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@

aica_sequence.o: $(ARM)/aica_sequence.c $(ARM)/aica_sequence.h
	kos-cc $(KOS_CFLAGS) -c $< -o $@
