/* KallistiOS ##version##

   main.c
   Copyright (C) 2026 Joseph Black

   Demonstrate a synchronized key-on spanning AICA channels 31 and 32.
*/

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos.h>
#include <dc/spu.h>
#include <dc/sound/aica_comm.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/sound.h>

#define SAMPLE_FRAMES 256
#define CHANNEL_COUNT 33
#define LEFT_CHANNEL   31
#define RIGHT_CHANNEL  32

static int16_t samples[SAMPLE_FRAMES];

static void make_waveform(void) {
    size_t i;

    for(i = 0; i < SAMPLE_FRAMES; ++i) {
        int phase = (int)(i & 63);

        samples[i] = (int16_t)(phase < 32
            ? -12000 + phase * 750
            : 12000 - (phase - 32) * 750);
    }
}

static int queue_delayed_start(int channel, uint32_t base, uint32_t pan) {
    AICA_CMDSTR_CHANNEL(packet, command, channel_data);

    memset(packet, 0, sizeof(packet));
    command->cmd = AICA_CMD_CHAN;
    command->size = AICA_CMDSTR_CHANNEL_SIZE;
    command->cmd_id = (uint32_t)channel;

    channel_data->cmd = AICA_CH_CMD_START | AICA_CH_START_DELAY;
    channel_data->base = base;
    channel_data->type = AICA_SM_16BIT;
    channel_data->length = SAMPLE_FRAMES;
    channel_data->loop = 1;
    channel_data->loopstart = 0;
    channel_data->loopend = SAMPLE_FRAMES;
    channel_data->freq = 22050;
    channel_data->vol = 200;
    channel_data->pan = pan;

    return snd_sh4_to_aica(packet, command->size);
}

int main(int argc, char **argv) {
    uint32_t sample_address = 0;
    snd_driver_status_t driver_status;
    int channels[CHANNEL_COUNT];
    int allocated = 0;
    int result = EXIT_FAILURE;
    int i;

    (void)argc;
    (void)argv;

    if(snd_init() < 0) {
        perror("snd_init");
        return EXIT_FAILURE;
    }

    if(snd_driver_get_status(&driver_status, 100) < 0) {
        perror("snd_driver_get_status");
        goto cleanup;
    }

    printf("AICA firmware %08" PRIx32 ", features %08" PRIx32 "\n",
           driver_status.firmware_version, driver_status.features);

    for(i = 0; i < CHANNEL_COUNT; ++i) {
        channels[i] = snd_sfx_chn_alloc();
        if(channels[i] < 0) {
            perror("snd_sfx_chn_alloc");
            goto cleanup;
        }

        ++allocated;
    }

    if(channels[LEFT_CHANNEL] != LEFT_CHANNEL ||
       channels[RIGHT_CHANNEL] != RIGHT_CHANNEL) {
        errno = EBUSY;
        perror("channels 31 and 32 are unavailable");
        goto cleanup;
    }

    make_waveform();
    sample_address = snd_mem_malloc(sizeof(samples));
    if(!sample_address) {
        perror("snd_mem_malloc");
        goto cleanup;
    }

    spu_memload(sample_address, samples, sizeof(samples));

    /* Keep queue processing stopped until both channels and their shared
       64-channel key-on command have been published as one batch. */
    snd_sh4_to_aica_stop();

    if(queue_delayed_start(LEFT_CHANNEL, sample_address, 0) < 0 ||
       queue_delayed_start(RIGHT_CHANNEL, sample_address, 255) < 0 ||
       snd_channels_start_sync((UINT64_C(1) << LEFT_CHANNEL) |
                               (UINT64_C(1) << RIGHT_CHANNEL)) < 0) {
        int saved_errno = errno;

        snd_sh4_to_aica_start();
        errno = saved_errno;
        perror("synchronized channel submission");
        goto cleanup;
    }

    snd_sh4_to_aica_start();
    puts("Channels 31 and 32 started from one synchronized key-on.");
    thd_sleep(2000);
    result = EXIT_SUCCESS;

cleanup:
    if(allocated > LEFT_CHANNEL)
        snd_sfx_stop(LEFT_CHANNEL);
    if(allocated > RIGHT_CHANNEL)
        snd_sfx_stop(RIGHT_CHANNEL);
    if(sample_address)
        snd_mem_free(sample_address);

    while(allocated)
        snd_sfx_chn_free(channels[--allocated]);

    snd_shutdown();
    return result;
}
