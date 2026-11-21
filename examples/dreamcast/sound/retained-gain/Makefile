TARGET = retained-gain.elf
OBJS = main.o

all: $(TARGET)
include $(KOS_BASE)/Makefile.rules

AUDIO = $(KOS_BASE)/kernel/arch/dreamcast/sound
KOS_CFLAGS += -I$(AUDIO)
main.o: $(AUDIO)/snd_retained_pcm.h $(AUDIO)/snd_retained_gain.h

$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a
	kos-cc -o $@ $(OBJS)

clean:
	$(RM) $(TARGET) $(OBJS)
