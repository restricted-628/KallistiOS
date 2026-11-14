all: stream.drv

aica_fw.h: stream.drv
	$(KOS_BASE)/utils/bin2c/bin2c stream.drv aica_fw.h.tmp
	echo 'unsigned char aica_fw[] = {' > aica_fw.h
	cat aica_fw.h.tmp >> aica_fw.h
	echo '};' >> aica_fw.h
	-rm aica_fw.h.tmp

ARM_CC_IS_AVAILABLE=0
ifdef DC_ARM_CC
  ifneq ("$(wildcard $(DC_ARM_CC))", "") 
    ARM_CC_IS_AVAILABLE=1
  endif
endif

ifeq ($(ARM_CC_IS_AVAILABLE), 1)
# Only compile this if we have an ARM compiler handy
stream.drv: prog.elf
	$(DC_ARM_OBJCOPY) -O binary $< $@
else
# Otherwise use precompiled ARM binary
stream.drv: stream.drv.prebuilt
	cp $< $@
endif

ASQ_DIR = $(KOS_BASE)/utils/aica-sequence-tool/native
ASQ_OBJS = aica_sequence_resource.o aica_sequence.o asq_native.o asq_catalog.o asq_wire.o

MUSIC_OBJS = aica_music.o aica_music_bank.o aica_music_wire.o aica_music_live.o

prog.elf: crt0.o main.o aica.o aica_observer.o aica_bank_registry.o $(MUSIC_OBJS) $(ASQ_OBJS)
	$(DC_ARM_CC) -Wl,-Ttext,0x00000000,-Map,prog.map,-N -nostartfiles -nostdlib -e reset -o prog.elf crt0.o main.o aica.o aica_observer.o aica_bank_registry.o $(MUSIC_OBJS) $(ASQ_OBJS) -lgcc

%.o: %.c
	$(DC_ARM_CC) $(DC_ARM_CFLAGS) $(DC_ARM_INCS) -I. -I$(ASQ_DIR) -I $(KOS_BASE)/kernel/arch/dreamcast/include -I $(KOS_BASE)/kernel/arch/dreamcast/include/dc/sound -c $< -o $@

%.o: %.s
	$(DC_ARM_AS) $(DC_ARM_AFLAGS) $< -o $@

clean:
	-rm -f *.o *.srec *.elf 1ST_READ.BIN prog.bin *.bck prog.map stream.drv

main.o aica_observer.o: aica_observer.h $(KOS_BASE)/kernel/arch/dreamcast/include/dc/sound/aica_observation.h

main.o aica_observer.o: $(KOS_BASE)/kernel/arch/dreamcast/include/dc/sound/aica_completion.h
aica.o main.o: aica.h
main.o aica_bank_registry.o: aica_bank_registry.h $(KOS_BASE)/kernel/arch/dreamcast/include/dc/sound/aica_bank_registry.h

main.o $(MUSIC_OBJS): aica_music_live.h aica_music.h aica_music_bank.h aica_music_wire.h aica_music_sequence.h

main.o aica_sequence_resource.o: aica_sequence_resource.h $(ASQ_DIR)/wire.h $(ASQ_DIR)/catalog.h $(ASQ_DIR)/asq.h
main.o $(ASQ_OBJS): aica_sequence.h $(ASQ_DIR)/asq.h
main.o aica_sequence_resource.o: $(KOS_BASE)/kernel/arch/dreamcast/include/dc/sound/aica_sequence_resource.h $(KOS_BASE)/kernel/arch/dreamcast/include/dc/sound/aica_music_live.h
asq_wire.o: $(ASQ_DIR)/asq.h aica_cmd_iface.h
asq_native.o: $(ASQ_DIR)/asq.c $(ASQ_DIR)/asq.h aica_sequence.h
	$(DC_ARM_CC) $(DC_ARM_CFLAGS) $(DC_ARM_INCS) -I. -I$(KOS_BASE)/kernel/arch/dreamcast/include -c $< -o $@
asq_catalog.o: $(ASQ_DIR)/catalog.c $(ASQ_DIR)/catalog.h $(ASQ_DIR)/asq.h
	$(DC_ARM_CC) $(DC_ARM_CFLAGS) $(DC_ARM_INCS) -I. -I$(KOS_BASE)/kernel/arch/dreamcast/include -c $< -o $@
asq_wire.o: $(ASQ_DIR)/wire.c $(ASQ_DIR)/wire.h $(ASQ_DIR)/catalog.h
	$(DC_ARM_CC) $(DC_ARM_CFLAGS) $(DC_ARM_INCS) -I. -I$(KOS_BASE)/kernel/arch/dreamcast/include -c $< -o $@
