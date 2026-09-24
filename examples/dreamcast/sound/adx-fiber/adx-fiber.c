/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Synthetic low-volume audio; no SDK, file access, or commercial assets.
*/
#include <kos.h>
#include <kos/fiber_service.h>
#include <arch/mmu.h>
#include <dc/cache.h>
#include <dc/sound/adx_pipe.h>
#include <dc/sound/adx_input.h>
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
#define INPUT_BYTES (36u + ((FRAMES + 31u) / 32u) * 18u)

#ifndef ADX_OIX
#define ADX_OIX 0
#endif
#ifndef ADX_MAPPED
#define ADX_MAPPED 1
#endif
#define DECODE_VIRTUAL 0x0e000000u
#define WORKSPACE_READY 0x41445831u
#define WORKSPACE_DONE 0x41445832u

/* Only this translated view is used while the decoder is live. Ordinary
   P1 input/PCM buffers are disjoint, not aliases of these physical pages. */
typedef struct decode_workspace {
    snd_adx_pipe_t pipe;
    uint32_t marker;
} decode_workspace_t;
_Static_assert(sizeof(decode_workspace_t) <= PAGESIZE, "workspace page size");
static decode_workspace_t *workspace;
static snd_adx_pipe_t *pipe_state;
static decode_workspace_t *backing;
static mmucontext_t *decode_context;
static bool workspace_live;
static uint32_t original_oix;

static uint8_t header[36], input_storage[2048], loader_pending[127];
static snd_adx_input_t compressed;
static size_t source_position, pending_size, pending_used;
static alignas(32) int16_t ring[RING_FRAMES * 2];
static alignas(32) int16_t scratch[SOUND_BYTES / 2];
static alignas(32) uint8_t decode_stack[8192], other_stack[8192];
static fiber_service_t *decoder_service;
static uint32_t other_steps;
/* Accessed only by main/the synchronous stream callback on main. */
static uint64_t scheduled_bytes, last_pcm_end;
static unsigned starvations;
static bool callback_failed;

static int workspace_open(void) {
    uint32_t ccr = *(volatile uint32_t *)0xff00001c;
    /* This standalone probe owns the address space and cache-mode transition.
       A library must instead negotiate both with the application's owner. */
    if(!mmu_enabled() || mmu_cxt_current || (ccr & (CCR_ORA | CCR_OIX))) {
        printf("ADX-FIBER: requires startup MMU, no context, and normal cache\n");
        return -1;
    }
    original_oix = ccr & CCR_OIX;
    backing = aligned_alloc(PAGESIZE, PAGESIZE);
    if(!backing)
        return -1;
    memset(backing, 0, PAGESIZE);
    backing->marker = WORKSPACE_READY;
    uintptr_t physical = (uintptr_t)backing & MEM_AREA_CACHE_MASK;
    if(physical < 0x0c000000u || physical > 0x0c000000u + HW_MEMSIZE - PAGESIZE ||
       (physical & 0x02000000u))
        return -1;
    dcache_purge_range((uintptr_t)backing, PAGESIZE);
    if(!ADX_MAPPED) {
        /* Explicit control run: MMU on, but no translated decoder storage. */
        workspace = backing;
        pipe_state = &workspace->pipe;
        if(ADX_OIX)
            dcache_toggle_ocindex(true);
        workspace_live = true;
        printf("ADX-MMU: direct-control ram=%lu oix=%u\n",
               (unsigned long)HW_MEMSIZE, (unsigned)ADX_OIX);
        return 0;
    }
    workspace = (void *)DECODE_VIRTUAL;
    pipe_state = &workspace->pipe;
    decode_context = mmu_context_create(1);
    if(!decode_context || mmu_page_map_ex(decode_context,
            DECODE_VIRTUAL >> PAGESIZE_BITS, physical >> PAGESIZE_BITS, 1,
            MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true) < 0)
        return -1;
    mmu_use_table(decode_context);
    mmu_switch_context(decode_context);
    if(ADX_OIX)
        dcache_toggle_ocindex(true);

    /* Do not let a cacheless/SQ-only emulator produce a misleading PASS. */
    if(*(volatile uint32_t *)&workspace->marker != WORKSPACE_READY) {
        printf("ADX-FIBER: translated workspace unavailable; no decode run\n");
        return -1;
    }
    workspace_live = true;
    printf("ADX-MMU: ram=%lu va=%08lx pa=%08lx oix=%u\n",
           (unsigned long)HW_MEMSIZE, (unsigned long)DECODE_VIRTUAL,
           (unsigned long)physical, (unsigned)ADX_OIX);
    return 0;
}

static bool workspace_close(bool decoded) {
    /* Called only after audio and executor users are gone. Destroy while OIX
       is still active to exercise physical-tag retirement of colored pages. */
    if(decode_context) {
        mmu_context_destroy(decode_context);
        decode_context = NULL;
    }
    if(!ADX_MAPPED && backing)
        dcache_purge_range((uintptr_t)backing, PAGESIZE);
    bool intact = !decoded || (backing &&
        ((volatile decode_workspace_t *)(((uintptr_t)backing & MEM_AREA_CACHE_MASK)
                                         | MEM_AREA_P2_BASE))->marker == WORKSPACE_DONE);
    if(workspace_live || backing)
        cache_write_ccr(CCR_OIX, original_oix);
    free(backing);
    backing = NULL;
    workspace_live = false;
    return intact;
}

static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (24 - i * 8));
}

static void make_synthetic_adx(void) {
    header[0] = 0x80; header[3] = 32;
    header[4] = 3; header[5] = 18; header[6] = 4; header[7] = 1;
    put32(header + 8, RATE); put32(header + 12, FRAMES);
    header[16] = 1; header[17] = 244; header[18] = 5;
    memcpy(header + 30, "(c)CRI", 6);
}

/* Bounded, nonblocking synthetic feeder on main. A real blocking file reader
   belongs on a separate ordinary loader thread, not this audio-poll owner or
   the decoder fiber. Retain pending suffixes and close FAILED on I/O errors. */
static void feed_input(void) {
    if(snd_adx_input_get_end(&compressed) != SND_ADX_INPUT_OPEN)
        return;
    for(unsigned budget = 0; budget < 4; ++budget) {
        if(pending_used == pending_size) {
            if(source_position == INPUT_BYTES) {
                snd_adx_input_close(&compressed, SND_ADX_INPUT_EOF);
                fiber_service_wake(decoder_service);
                return;
            }
            pending_size = INPUT_BYTES - source_position;
            if(pending_size > sizeof(loader_pending))
                pending_size = sizeof(loader_pending);
            for(size_t i = 0; i < pending_size; ++i) {
                size_t offset = source_position + i;
                if(offset < sizeof(header))
                    loader_pending[i] = header[offset];
                else {
                    unsigned byte = (offset - sizeof(header)) % 18;
                    loader_pending[i] = byte == 0 ? 0 : byte == 1 ? 15 :
                                        byte < 10 ? 0x11 : 0xff;
                }
            }
            source_position += pending_size;
            pending_used = 0;
        }
        size_t accepted = snd_adx_input_write(&compressed,
                            loader_pending + pending_used, pending_size - pending_used);
        pending_used += accepted;
        if(!accepted)
            return;
        fiber_service_wake(decoder_service);
    }
}

static void decode_service(fiber_service_t *self, void *unused) {
    (void)unused;
    for(;;) {
        if(fiber_service_stop_requested(self))
            snd_adx_pipe_request_cancel(pipe_state);
        snd_adx_input_result_t step = snd_adx_input_step(&compressed, pipe_state);
        snd_adx_result_t r = step.codec;
        if(r.status >= SND_ADX_DONE) {
            if(r.status == SND_ADX_DONE)
                workspace->marker = WORKSPACE_DONE;
            return;
        }
        if(r.status == SND_ADX_NEED_OUTPUT || r.status == SND_ADX_NEED_INPUT) {
            /* Wake requests are latched: consumer progress just before this
               wait must not be lost. Cancellation also wakes this service. */
            if(fiber_service_wait(self, 0) < 0)
                snd_adx_pipe_request_cancel(pipe_state);
        }
        else if(fiber_service_yield(self) < 0)
            snd_adx_pipe_request_cancel(pipe_state);
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
    snd_adx_pipe_result_t r = snd_adx_pipe_read(pipe_state, scratch,
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
    if(workspace_open() < 0) {
        workspace_close(false);
        return EXIT_FAILURE;
    }
    make_synthetic_adx();
    if(snd_adx_pipe_init(pipe_state, ring, RING_FRAMES * 2, RING_FRAMES) < 0)
        goto cleanup;
    if(snd_adx_input_init(&compressed, input_storage, sizeof(input_storage)) < 0)
        goto cleanup;

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
        feed_input();
        snd_adx_pipe_result_t r = snd_adx_pipe_read(pipe_state, NULL, 0);
        if(r.producer >= SND_ADX_INVALID)
            goto cleanup;
        if(r.queued_frames >= SOUND_BYTES / 2)
            break;
        if(timer_ms_gettime64() >= deadline)
            goto cleanup;
        thd_sleep(1);
    }
    snd_adx_info_t info;
    if(!snd_adx_pipe_get_info(pipe_state, &info) || info.channels != 1)
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
        feed_input();
        /* Main is the ordinary audio thread. Blocking sound APIs never run
           on the shared fiber executor. Only this owner controls the stream. */
        if(snd_stream_poll_ex(stream) < 0 && errno != ENODATA)
            goto cleanup;
        if(callback_failed || snd_stream_get_status(stream, &status) < 0)
            goto cleanup;
        snd_adx_pipe_result_t r = snd_adx_pipe_read(pipe_state, NULL, 0);
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
    snd_adx_pipe_request_cancel(pipe_state);
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
    if(!workspace_close(success))
        success = false;
    printf("ADX-FIBER: %s, starvation_callbacks=%u other_steps=%lu "
           "played_bytes=%llu last_pcm_end=%llu\n",
           success ? "PASS" : "FAIL", starvations,
           (unsigned long)__atomic_load_n(&other_steps, __ATOMIC_RELAXED),
           (unsigned long long)status.played_bytes,
           (unsigned long long)last_pcm_end);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
