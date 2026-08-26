# KallistiOS ##version##
#
# arch/dreamcast/hardware/Makefile
# (c)2000-2001 Megan Potter
#

# Init wrapper
OBJS = hardware.o

# BIOS services
ifneq ($(KOS_SUBARCH), naomi)
	OBJS += biosfont.o cdrom.o cdrom_request.o cdrom_range.o expansion.o \
		g1_bus.o gdrom_spi.o gdrom_direct.o flashrom.o flashrom_layout.o
endif

# System Calls
OBJS += syscalls.o syscall_font.o dcload.o dcload_syscalls.o dcload_syscalls_net.o

# G2
OBJS += g2dma.o rtc.o rtc_calendar.o

# Sound
OBJS += spu.o spu_request.o

# Bus support
OBJS += asic.o g2bus.o

# Video-related
OBJS += video.o video_mode.o video_raster.o vblank.o

# CPU-related
OBJS += sq.o sq_fast_cpy.o scif.o scif_config.o sci.o ubc.o dmac.o wdt.o

# SPI device support
OBJS += scif-spi.o sd.o

# G1 Bus ATA support
ifneq ($(KOS_SUBARCH), naomi)
	OBJS += g1ata.o
endif

SUBDIRS = pvr maple

ifneq ($(KOS_SUBARCH), naomi)
	SUBDIRS += network modem
endif

include $(KOS_BASE)/Makefile.prefab
