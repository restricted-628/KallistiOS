/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Synthetic low-volume audio; no SDK, file access, or commercial assets.
*/
#include <kos.h>
#include <dc/sound/adx_pipe.h>
#include <dc/sound/stream.h>
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100u
#define FRAMES (RATE * 2u + 1u)
#define RING_FRAMES 8192u
#define SOUND_BYTES 8192u

static uint8_t encoded[36 + ((FRAMES + 31) / 32) * 18];
static alignas(32) int16_t ring[RING_FRAMES * 2];
static alignas(32) int16_t scratch[SOUND_BYTES / 2];
static alignas(32) uint8_t decode_stack[8192], other_stack[8192];
static snd_adx_pipe_t pipe_state;
static fiber_service_t *decoder_service;
static uint32_t other_steps;
/* Accessed only by main/the synchronous stream callback on main. */
static uint64_t scheduled_bytes, last_pcm_end;
static unsigned starvations;
static bool callback_failed;

static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (24 - i * 8));
}

static void make_synthetic_adx(void) {
    encoded[0] = 0x80; encoded[3] = 32;
    encoded[4] = 3; encoded[5] = 18; encoded[6] = 4; encoded[7] = 1;
    put32(encoded + 8, RATE); put32(encoded + 12, FRAMES);
    encoded[16] = 1; encoded[17] = 244; encoded[18] = 5;
    memcpy(encoded + 30, "(c)CRI", 6);
    for(size_t at = 36; at < sizeof(encoded); at += 18) {
        encoded[at + 1] = 15;
        for(unsigned i = 2; i < 18; ++i)
            encoded[at + i] = (i < 10) ? 0x11 : 0xff;
    }
}

static void decode_service(fiber_service_t *self, void *unused) {
    size_t pos = 0;
    (void)unused;
    for(;;) {
        if(fiber_service_stop_requested(self))
            snd_adx_pipe_request_cancel(&pipe_state);
        size_t count = sizeof(encoded) - pos;
        if(count > 127)
            count = 127;
        snd_adx_result_t r = snd_adx_pipe_decode(&pipe_state, encoded + pos,
                                         count, pos + count == sizeof(encoded));
        pos += r.consumed;
        if(r.status >= SND_ADX_DONE)
            return;
        if(r.status == SND_ADX_NEED_OUTPUT) {
            /* Wake requests are latched: consumer progress just before this
               wait must not be lost. Cancellation also wakes this service. */
            if(fiber_service_wait(self, 0) < 0)
                snd_adx_pipe_request_cancel(&pipe_state);
        }
        else if(fiber_service_yield(self) < 0)
            snd_adx_pipe_request_cancel(&pipe_state);
    }
}

static void other_service(fiber_service_t *self, void *unused) {
    (void)unused;
    while(!fiber_service_stop_requested(self)) {
        __atomic_add_fetch(&other_steps, 1, __ATOMIC_RELAXED);
        /* Deliberately keeps unrelated work runnable beside the decoder. */
        if(fiber_service_yield(self) < 0)
            return;
    }
}

static void *audio_callback(snd_stream_hnd_t stream, int requested, int *received) {
    (void)stream;
    *received = 0;
    if(requested < 0 || (unsigned)requested > sizeof(scratch) || (requested & 1)) {
        callback_failed = true;
        return NULL;
    }
    snd_adx_pipe_result_t r = snd_adx_pipe_read(&pipe_state, scratch,
                                               (size_t)requested / 2);
    *received = (int)(r.frames * 2);
    if(r.frames) {
        last_pcm_end = scheduled_bytes + r.frames * 2;
        (void)fiber_service_wake(decoder_service);
    }
    scheduled_bytes += (unsigned)requested;
    if(r.starved)
        ++starvations;
    if(r.producer >= SND_ADX_INVALID)
        callback_failed = true;
    /* KOS copies scratch before invoking us again. Producer cannot touch it.
       KOS, not this callback, pads a short result with silence. */
    return r.frames ? scratch : NULL;
}

static void retain_on_failure(const char *operation) {
    printf("ADX-FIBER: %s failed (%d); retaining live buffers. Reset required.\n",
           operation, errno);
    for(;;)
        thd_sleep(1000);
}

int main(int argc, char **argv) {
    fiber_service_executor_t *executor = NULL;
    fiber_service_t *other = NULL;
    snd_stream_hnd_t stream = SND_STREAM_INVALID;
    bool sound_initialized = false, success = false;
    snd_stream_status_t status = {0};
    (void)argc; (void)argv;
    make_synthetic_adx();
    if(snd_adx_pipe_init(&pipe_state, ring, RING_FRAMES * 2, RING_FRAMES) < 0)
        return EXIT_FAILURE;

    executor = fiber_service_executor_create_ex(KFIBER_ATTACH_MATH_CONTEXT);
    if(!executor)
        goto cleanup;
    decoder_service = fiber_service_add(executor, decode_stack, sizeof(decode_stack),
                                         decode_service, NULL);
    other = fiber_service_add(executor, other_stack, sizeof(other_stack), other_service, NULL);
    if(!decoder_service || !other || fiber_service_executor_start(executor, NULL) < 0)
        goto cleanup;
    fiber_service_wake(decoder_service);
    fiber_service_wake(other);

    uint64_t deadline = timer_ms_gettime64() + 5000;
    for(;;) {
        snd_adx_pipe_result_t r = snd_adx_pipe_read(&pipe_state, NULL, 0);
        if(r.producer >= SND_ADX_INVALID)
            goto cleanup;
        if(r.queued_frames >= SOUND_BYTES / 2)
            break;
        if(timer_ms_gettime64() >= deadline)
            goto cleanup;
        thd_sleep(1);
    }
    snd_adx_info_t info;
    if(!snd_adx_pipe_get_info(&pipe_state, &info) || info.channels != 1)
        goto cleanup;
    if(snd_stream_init_ex(1, SOUND_BYTES) < 0)
        goto cleanup;
    sound_initialized = true;
    stream = snd_stream_alloc(audio_callback, SOUND_BYTES);
    if(stream == SND_STREAM_INVALID)
        goto cleanup;
    snd_stream_config_t config;
    if(snd_stream_config_init(&config) < 0)
        goto cleanup;
    config.sample_rate = info.sample_rate;
    config.channel[0].volume = 32;
    if(snd_stream_start_ex(stream, &config) < 0)
        goto cleanup;

    uint32_t other_before = __atomic_load_n(&other_steps, __ATOMIC_RELAXED);
    deadline = timer_ms_gettime64() + 10000;
    while(timer_ms_gettime64() < deadline) {
        /* Main is the ordinary audio thread. Blocking sound APIs never run
           on the shared fiber executor. Only this owner controls the stream. */
        if(snd_stream_poll_ex(stream) < 0 && errno != ENODATA)
            goto cleanup;
        if(callback_failed || snd_stream_get_status(stream, &status) < 0)
            goto cleanup;
        snd_adx_pipe_result_t r = snd_adx_pipe_read(&pipe_state, NULL, 0);
        if(r.drained && r.producer == SND_ADX_DONE &&
           !status.dma_pending && status.source_bytes == FRAMES * 2u &&
           status.played_bytes >= last_pcm_end) {
            success = last_pcm_end != 0 &&
                      __atomic_load_n(&other_steps, __ATOMIC_RELAXED) != other_before;
            break;
        }
        thd_sleep(5);
    }

cleanup:
    /* No more start/poll calls: no further callbacks can wake the producer.
       Stop and drain AICA before any stream/buffer/executor reclamation. */
    snd_adx_pipe_request_cancel(&pipe_state);
    if(decoder_service)
        fiber_service_request_stop(decoder_service);
    if(other)
        fiber_service_request_stop(other);
    if(stream != SND_STREAM_INVALID) {
        if(snd_stream_stop_ex(stream, 2000) < 0)
            retain_on_failure("stop/drain");
        if(snd_stream_destroy_ex(stream, 2000) < 0)
            retain_on_failure("stream destroy");
    }
    if(executor && fiber_service_executor_destroy(executor) < 0)
        retain_on_failure("executor destroy");
    if(sound_initialized)
        snd_stream_shutdown();
    printf("ADX-FIBER: %s, starvation_callbacks=%u other_steps=%lu "
           "played_bytes=%llu last_pcm_end=%llu\n",
           success ? "PASS" : "FAIL", starvations,
           (unsigned long)__atomic_load_n(&other_steps, __ATOMIC_RELAXED),
           (unsigned long long)status.played_bytes,
           (unsigned long long)last_pcm_end);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
