/* KallistiOS ##version##

   main.c
   Copyright (C) 2026 Joseph Black

   Exercise the optional caller-sized sound-stream polling service.
*/

#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kos.h>
#include <dc/sound/stream.h>
#include <dc/sound/stream_service.h>

#define STREAM_BUFFER_BYTES 32768
#define SERVICE_STACK_BYTES 32768
#define RUN_TIME_MS         1000

static alignas(32) int16_t callback_buffer[STREAM_BUFFER_BYTES / 2];
static alignas(THD_STACK_ALIGNMENT) uint8_t service_stack[SERVICE_STACK_BYTES];
static uint32_t phase;

static void *stream_callback(snd_stream_hnd_t handle, int requested,
                             int *received) {
    size_t frame;
    size_t frames = (size_t)requested / (2 * sizeof(int16_t));

    (void)handle;
    for(frame = 0; frame < frames; ++frame) {
        int16_t sample = (int16_t)((phase & 63) < 32 ? -7000 : 7000);

        callback_buffer[frame * 2] = sample;
        callback_buffer[frame * 2 + 1] = sample;
        ++phase;
    }

    *received = requested;
    return callback_buffer;
}

int main(int argc, char **argv) {
    kthread_attr_t thread_attr = {
        .stack_size = sizeof(service_stack),
        .stack_ptr = service_stack,
        .prio = PRIO_DEFAULT,
        .label = "[stream-example]",
    };
    snd_stream_service_status_t service_status;
    snd_stream_config_t config;
    snd_stream_status_t stream_status;
    snd_stream_service_t *service = NULL;
    snd_stream_hnd_t stream = SND_STREAM_INVALID;
    bool registered = false;
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
        goto destroy_stream;
    }
    config.channels = 2;
    config.channel[0].pan = 0;
    config.channel[1].pan = 255;
    if(snd_stream_start_ex(stream, &config) < 0) {
        perror("snd_stream_start_ex");
        goto destroy_stream;
    }

    service = snd_stream_service_create(5, &thread_attr, NULL, NULL);
    if(!service) {
        perror("snd_stream_service_create");
        goto destroy_stream;
    }
    if(snd_stream_service_add(service, stream) < 0) {
        perror("snd_stream_service_add");
        goto destroy_service;
    }
    registered = true;

    if(snd_stream_poll_ex(stream) == 0 || errno != EBUSY) {
        errno = EPROTO;
        perror("exclusive service ownership");
        goto remove_stream;
    }

    thd_sleep(RUN_TIME_MS);
    if(snd_stream_service_get_status(service, &service_status) < 0 ||
       snd_stream_get_status(stream, &stream_status) < 0) {
        perror("stream service status");
        goto remove_stream;
    }
    if(service_status.state != SND_STREAM_SERVICE_RUNNING ||
       service_status.registered_streams != 1 ||
       !service_status.polls || !service_status.successful_polls ||
       service_status.hard_errors || !stream_status.played_bytes ||
       !stream_status.service_owned) {
        errno = EPROTO;
        perror("stream service validation");
        goto remove_stream;
    }

    printf("sweeps=%llu polls=%llu played=%llu\n",
           (unsigned long long)service_status.sweeps,
           (unsigned long long)service_status.polls,
           (unsigned long long)stream_status.played_bytes);
    result = EXIT_SUCCESS;

remove_stream:
    if(registered && snd_stream_service_remove(service, stream, 1000) < 0) {
        perror("snd_stream_service_remove");
        result = EXIT_FAILURE;
    }
    else {
        registered = false;
    }
    if(result == EXIT_SUCCESS &&
       (snd_stream_get_status(stream, &stream_status) < 0 ||
        stream_status.service_owned ||
        (snd_stream_poll_ex(stream) < 0 && errno != ENODATA))) {
        errno = EPROTO;
        perror("released service ownership");
        result = EXIT_FAILURE;
    }
destroy_service:
    if(service && snd_stream_service_destroy(service, 1000) < 0) {
        perror("snd_stream_service_destroy");
        result = EXIT_FAILURE;
    }
destroy_stream:
    if(stream != SND_STREAM_INVALID &&
       snd_stream_destroy_ex(stream, 1000) < 0) {
        perror("snd_stream_destroy_ex");
        result = EXIT_FAILURE;
    }
shutdown:
    snd_stream_shutdown();
    return result;
}
