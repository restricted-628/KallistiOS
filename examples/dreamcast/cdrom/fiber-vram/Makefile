# KallistiOS ##version##
TARGET = fiber-vram.elf
OBJS = fiber-vram.o
HOST_CC ?= cc
ASSET = texture.bin

.PHONY: all clean
all: $(TARGET) $(ASSET)
include $(KOS_BASE)/Makefile.rules

fiber-vram.o: texture-pattern.h
make-texture.exe: make-texture.c texture-pattern.h
	$(HOST_CC) -std=c17 -O2 -Wall -Wextra -Werror -o $@ make-texture.c
$(ASSET): make-texture.exe
	./make-texture.exe $@
$(TARGET): $(OBJS) $(KOS_BASE)/lib/$(KOS_ARCH)/libkallisti.a \
           $(KOS_BASE)/addons/lib/$(KOS_ARCH)/libfiber_disc.a
	kos-cc -Wl,-Map,$@.map -o $@ $(OBJS) -lfiber_disc
clean:
	-rm -f $(TARGET) $(OBJS) $(ASSET) make-texture.exe
