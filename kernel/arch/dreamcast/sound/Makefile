# KallistiOS ##version##
#
# libmp3/Makefile
# (c)2001 Megan Potter
# (c)2023 Ruslan Rostovtsev
#

OBJS = snd_iface.o \
	snd_adx.o \
	snd_adx_pipe.o \
	snd_adx_input.o \
	snd_dsp.o \
	snd_dsp_program.o \
	snd_sfxmgr.o \
	snd_channel.o \
	snd_port.o \
	snd_observation.o \
	snd_playback.o \
	snd_stream.o \
	snd_stream_service.o \
	snd_mem.o \
	snd_pcm_split.o \
	snd_pcm16_sq.o

KOS_CFLAGS += -I $(KOS_BASE)/kernel/arch/dreamcast/include/dc/sound

SUBDIRS = arm

include $(KOS_BASE)/Makefile.prefab

# The recursive build may replace this file. A recipe makes make re-read its
# timestamp after subdirs, so the embedding cannot retain the previous image.
arm/stream.drv: subdirs
	@test -f $@

snd_iface.o: snd_stream_drv.c

snd_stream_drv.c: arm/stream.drv $(KOS_BASE)/utils/bin2c/bin2c
	$(KOS_BASE)/utils/bin2c/bin2c $< $@ snd_stream_drv

clean:
	-rm -f snd_stream_drv.c
