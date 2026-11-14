TARGET = music-live.elf
BANK = $(KOS_BASE)/utils/aica-bank-tool/native
CLIENT = $(KOS_BASE)/utils/aica-music-client
ARM = $(KOS_BASE)/kernel/arch/dreamcast/sound/arm
OBJS = main.o abk.o abk_upload.o abk_registry.o music_client.o music_live.o
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
KOS_CFLAGS += -I$(BANK) -I$(CLIENT) -I$(ARM) -I$(KOS_BASE)/kernel/arch/dreamcast/sound
bank.h: make-bank.py $(KOS_BASE)/utils/aica-bank-tool/aica_bank.py $(KOS_BASE)/utils/aica-bank-tool/fixture.py
	python3 make-bank.py
main.o: bank.h
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
	kos-cc -Wl,-Map,music-live.map -o $@ $(OBJS)
clean:
	-rm -f $(TARGET) $(OBJS) bank.h music-live.map
run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

# Private decoded bank records change size with the parser header.
$(OBJS): $(BANK)/abk.h
