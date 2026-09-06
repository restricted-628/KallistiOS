/* KallistiOS ##version##

   main.c
   Copyright (C) 2026 Joseph Black

   Exercise checked stream lifecycle, progress, and underrun accounting.
*/

#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kos.h>
#include <dc/sound/stream.h>

#define STREAM_BUFFER_BYTES 32768
#define RUN_TIME_MS         1200

static alignas(32) int16_t callback_buffer[STREAM_BUFFER_BYTES / 2];
static unsigned int callback_count;

static void *stream_callback(snd_stream_hnd_t handle, int requested,
                             int *received) {
    size_t frame;
    size_t frames;
    int supplied = requested;

    (void)handle;
    ++callback_count;

    if(callback_count == 3)
        supplied -= 4;
    else if(callback_count == 4)
        supplied = 0;

    frames = (size_t)supplied / (2 * sizeof(int16_t));
    for(frame = 0; frame < frames; ++frame) {
        int phase = (int)(frame & 63);
        int16_t sample = (int16_t)(phase < 32 ?
            -8000 + phase * 500 : 8000 - (phase - 32) * 500);

        callback_buffer[frame * 2] = sample;
        callback_buffer[frame * 2 + 1] = sample;
    }

    *received = supplied;
    return supplied ? callback_buffer : NULL;
}

int main(int argc, char **argv) {
    snd_stream_config_t config;
    snd_stream_status_t status;
    snd_stream_hnd_t stream = SND_STREAM_INVALID;
    uint64_t started;
    bool updated = false;
    int result = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    if(snd_stream_init_ex(2, STREAM_BUFFER_BYTES) < 0) {
        perror("snd_stream_init_ex");
        return EXIT_FAILURE;
    }

    stream = snd_stream_alloc(stream_callback, STREAM_BUFFER_BYTES);
    if(stream == SND_STREAM_INVALID) {
        perror("snd_stream_alloc");
        goto shutdown;
    }

    if(snd_stream_config_init(&config) < 0) {
        perror("snd_stream_config_init");
        goto destroy;
    }
    config.channels = 2;
    config.channel[0].pan = 0;
    config.channel[1].pan = 255;

    snd_stream_queue_enable(stream);
    if(snd_stream_start_ex(stream, &config) < 0) {
        perror("snd_stream_start_ex");
        goto destroy;
    }
    if(snd_stream_get_status(stream, &status) < 0 ||
       status.state != SND_STREAM_STATE_QUEUED ||
       status.channel_playing[0] || status.channel_playing[1]) {
        errno = EPROTO;
        perror("queued stream validation");
        goto destroy;
    }
    if(snd_stream_queue_go_ex(stream) < 0) {
        perror("snd_stream_queue_go_ex");
        goto destroy;
    }

    started = timer_ms_gettime64();
    while(timer_ms_gettime64() - started < RUN_TIME_MS) {
        if(snd_stream_poll_ex(stream) < 0 && errno != ENODATA) {
            perror("snd_stream_poll_ex");
            goto destroy;
        }

        if(!updated && timer_ms_gettime64() - started >= 250) {
            config.channel[0].volume = 192;
            config.channel[1].volume = 192;
            config.channel[0].pan = 32;
            config.channel[1].pan = 224;
            if(snd_stream_update(stream, &config,
                                 SND_CHANNEL_UPDATE_VOLUME |
                                 SND_CHANNEL_UPDATE_PAN) < 0) {
                perror("snd_stream_update");
                goto destroy;
            }
            updated = true;
        }

        thd_sleep(5);
    }

    if(snd_stream_get_status(stream, &status) < 0) {
        perror("snd_stream_get_status");
        goto destroy;
    }
    if(!updated || !status.played_bytes || !status.underruns ||
       status.buffered_bytes <= status.source_bytes) {
        errno = EPROTO;
        perror("stream status validation");
        goto destroy;
    }

    printf("played=%llu source=%llu buffered=%llu underruns=%lu\n",
           (unsigned long long)status.played_bytes,
           (unsigned long long)status.source_bytes,
           (unsigned long long)status.buffered_bytes,
           (unsigned long)status.underruns);

    if(snd_stream_stop_ex(stream, 1000) < 0) {
        perror("snd_stream_stop_ex");
        goto destroy;
    }
    if(snd_stream_get_status(stream, &status) < 0 ||
       status.state != SND_STREAM_STATE_STOPPED ||
       status.channel_playing[0] || status.channel_playing[1]) {
        errno = EPROTO;
        perror("stopped stream validation");
        goto destroy;
    }

    result = EXIT_SUCCESS;

destroy:
    if(stream != SND_STREAM_INVALID && snd_stream_destroy_ex(stream, 1000) < 0)
        perror("snd_stream_destroy_ex");
shutdown:
    snd_stream_shutdown();
    return result;
}
