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

static int queue_delayed_start(int channel,
                               const snd_channel_config_t *base_config,
                               uint8_t pan) {
    snd_channel_config_t config = *base_config;

    config.pan = pan;
    return snd_channel_start(channel, &config, SND_CHANNEL_START_DELAYED);
}

static int wait_for_snapshots(snd_channel_status_ex_t *left,
                              snd_channel_status_ex_t *right) {
    uint64_t deadline = timer_ms_gettime64() + 100;

    do {
        int left_result = snd_channel_get_status_ex(LEFT_CHANNEL, left);

        if(left_result < 0 && errno != EAGAIN)
            return -1;

        if(left_result == 0) {
            int right_result = snd_channel_get_status_ex(RIGHT_CHANNEL, right);

            if(right_result < 0 && errno != EAGAIN)
                return -1;
            if(right_result == 0 && left->configured && right->configured &&
               left->config.pan == 0 && right->config.pan == 255 &&
               left->config.envelope.attack_rate == 24 &&
               right->config.envelope.release_rate == 20)
                return 0;
        }

        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    errno = ETIMEDOUT;
    return -1;
}

int main(int argc, char **argv) {
    uint32_t sample_address = 0;
    snd_channel_config_t channel_config;
    snd_channel_status_ex_t left_status;
    snd_channel_status_ex_t right_status;
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

    if(!(driver_status.features & SND_DRIVER_FEATURE_CHANNEL_CONTROL)) {
        errno = ENOTSUP;
        perror("checked channel control");
        goto cleanup;
    }

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

    if(snd_channel_config_init(&channel_config) < 0) {
        perror("snd_channel_config_init");
        goto cleanup;
    }

    channel_config.sample_address = sample_address;
    channel_config.sample_count = SAMPLE_FRAMES;
    channel_config.loop_enabled = true;
    channel_config.loop_end = SAMPLE_FRAMES;
    channel_config.sample_rate = 22050;
    channel_config.volume = 200;
    channel_config.envelope.attack_rate = 24;
    channel_config.envelope.release_rate = 20;

    /* Keep queue processing stopped until both channels and their shared
       64-channel key-on command have been published as one batch. */
    snd_sh4_to_aica_stop();

    if(queue_delayed_start(LEFT_CHANNEL, &channel_config, 0) < 0 ||
       queue_delayed_start(RIGHT_CHANNEL, &channel_config, 255) < 0 ||
       snd_channels_start_sync((UINT64_C(1) << LEFT_CHANNEL) |
                               (UINT64_C(1) << RIGHT_CHANNEL)) < 0) {
        int saved_errno = errno;

        snd_sh4_to_aica_start();
        errno = saved_errno;
        perror("synchronized channel submission");
        goto cleanup;
    }

    snd_sh4_to_aica_start();
    if(wait_for_snapshots(&left_status, &right_status) < 0) {
        perror("checked channel snapshot");
        goto cleanup;
    }

    puts("Channels 31 and 32 started from one synchronized key-on.");
    thd_sleep(2000);
    result = EXIT_SUCCESS;

cleanup:
    if(allocated > LEFT_CHANNEL)
        snd_channel_stop(LEFT_CHANNEL);
    if(allocated > RIGHT_CHANNEL)
        snd_channel_stop(RIGHT_CHANNEL);
    if(sample_address)
        snd_mem_free(sample_address);

    while(allocated)
        snd_sfx_chn_free(channels[--allocated]);

    snd_shutdown();
    return result;
}
