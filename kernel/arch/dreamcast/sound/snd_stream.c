/* KallistiOS ##version##

   snd_stream.c
   Copyright (C) 2000, 2001, 2002, 2003, 2004 Megan Potter
   Copyright (C) 2002 Florian Schulze
   Copyright (C) 2020 Lawrence Sebald
   Copyright (C) 2023, 2024, 2025, 2026 Ruslan Rostovtsev
   Copyright (C) 2024 Stefanos Kornilios Mitsis Poiitidis
   Copyright (C) 2026 Joseph Black

   SH-4 support routines for SPU streaming sound driver
*/

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/cdefs.h>
#include <sys/queue.h>

#include <kos/cache.h>
#include <kos/dbglog.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <dc/g2bus.h>
#include <dc/sq.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>
#include <dc/sound/stream.h>
#include <dc/sound/sfxmgr.h>

#include "arm/aica_cmd_iface.h"
#include "snd_stream_internal.h"
#include "snd_stream_split.h"

/*

This module uses a nice circularly queued data stream in SPU RAM, which is
looped by a program running in the SPU itself.

Basically the poll routine checks to see if a certain minimum amount of
data is available to the SPU to be played, and if not, we ask the user
routine for more sound data and load it up. That's about it.

This version is capable of playing back N streams at once, with the limit
being available CPU time and channels.

*/

typedef struct filter {
    TAILQ_ENTRY(filter) lent;
    snd_stream_filter_t func;
    void *data;
} filter_t;

/* Each of these represents an active streaming channel */
typedef struct strchan {
    /* Which AICA channels are we using? */
    int ch[2];

    /* The last write position in the playing buffer */
    uint32_t last_write_pos;

    /* The buffer size allocated for this stream. */
    size_t buffer_size;

    /* The format-dependent portion currently used as the circular buffer. */
    size_t active_buffer_size;

    /* Stream data location in AICA RAM */
    uint32_t spu_ram_sch[2];

    /* "Get data" callback; we'll call this any time we want to get
       another buffer of output data. */
    snd_stream_callback_t get_data;

    /* "Request data" callback; we'll call this any time we want to fill
       buffers of AICA channels directly. */
    snd_stream_callback_direct_t req_data;
    int callback_active;

    /* Our list of filter callback functions for this stream */
    TAILQ_HEAD(filterlist, filter) filters;

    /* Sample type */
    int type;

    /* Sample size */
    int bitsize;

    /* Stereo/mono flag */
    int channels;

    /* Playback frequency */
    int frequency;

    /* Checked controls and lifecycle observation. */
    snd_stream_config_t config;
    snd_stream_state_t state;
    int last_error;
    uint16_t last_play_pos;
    uint32_t play_position;
    uint64_t played_frames;
    uint64_t source_bytes;
    uint64_t buffered_bytes;
    uint32_t polls;
    uint32_t underruns;

    /* Stream queueing is where we get everything ready to go but don't
       actually start it playing until the signal (for music sync, etc) */
    int queueing;
    int queue_gate_held;
    uint64_t queued_start_channels;

    /* Have we been initialized yet? (and reserved a buffer, etc) */
    volatile int initted;

    /* User data. */
    void *user_data;

    uint32_t dma_length;
    uintptr_t dma_dest;
    volatile int dma_pending;
    volatile int dma_error;
    uint64_t dma_source_bytes;
    uint64_t dma_buffered_bytes;

    /* Non-NULL while one optional polling service owns this handle. */
    const void *service_owner;
} strchan_t;

/* Our stream structs */
static strchan_t streams[SND_STREAM_MAX];

/* Separation buffers (for stereo) */
static uint32_t *sep_buffer[2] = {NULL, NULL};

static semaphore_t stream_sem = SEM_INITIALIZER(1);
static mutex_t stream_state_mutex = RECURSIVE_MUTEX_INITIALIZER;

static int max_channels = 0;
static size_t max_buffer_size = 0;

/* Check an incoming handle */
#define CHECK_HND(x) do { \
        assert( (x) >= 0 && (x) < SND_STREAM_MAX ); \
        assert( streams[(x)].initted ); \
    } while(0)

static int snd_stream_fill(snd_stream_hnd_t hnd, uint32_t offset, size_t size,
                           size_t *source_bytes);
static int stream_dma_wait(strchan_t *stream, uint32_t timeout_ms);

static int checked_handle(snd_stream_hnd_t hnd) {
    if(hnd < 0 || hnd >= SND_STREAM_MAX || !streams[hnd].initted) {
        errno = EBADF;
        return -1;
    }

    return 0;
}

static uint64_t interleaved_bytes_for_frames(int bits, uint64_t frames,
                                             unsigned int channels) {
    switch(bits) {
        case 4:
            return (frames * channels) >> 1;
        case 8:
            return frames * channels;
        case 16:
        default:
            return frames * channels * 2;
    }
}

static size_t callback_frame_size(const strchan_t *stream) {
    if(stream->bitsize == 16)
        return 2u * (size_t)stream->channels;
    if(stream->bitsize == 8)
        return (size_t)stream->channels;

    /* The exact stereo splitter can silence-pad the unused high nibble when an
       ADPCM source ends after one interleaved frame. */
    return 1;
}

static void stream_set_error(strchan_t *stream, int error) {
    stream->state = SND_STREAM_STATE_ERROR;
    stream->last_error = error ? error : EIO;
}

static inline size_t samples_to_bytes(snd_stream_hnd_t hnd, size_t samples) {
    switch(streams[hnd].bitsize) {
        case 4:
            return samples >> 1;
        case 8:
            return samples;
        case 16:
        default:
            return samples << 1;
    }
}

static inline size_t bytes_to_samples(snd_stream_hnd_t hnd, size_t bytes) {
    switch(streams[hnd].bitsize) {
        case 4:
            return bytes << 1;
        case 8:
            return bytes;
        case 16:
        default:
            return bytes >> 1;
    }
}

/* Set "get data" callback */
void snd_stream_set_callback(snd_stream_hnd_t hnd, snd_stream_callback_t cb) {
    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);
    streams[hnd].get_data = cb;
    mutex_unlock(&stream_state_mutex);
}

void snd_stream_set_callback_direct(snd_stream_hnd_t hnd, snd_stream_callback_direct_t cb) {
    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);
    streams[hnd].req_data = cb;
    mutex_unlock(&stream_state_mutex);
}

void snd_stream_set_userdata(snd_stream_hnd_t hnd, void *d) {
    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);
    streams[hnd].user_data = d;
    mutex_unlock(&stream_state_mutex);
}

void *snd_stream_get_userdata(snd_stream_hnd_t hnd) {
    void *result;

    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);
    result = streams[hnd].user_data;
    mutex_unlock(&stream_state_mutex);
    return result;
}

void snd_stream_filter_add(snd_stream_hnd_t hnd, snd_stream_filter_t filtfunc, void * obj) {
    filter_t *f;

    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);

    if(streams[hnd].callback_active) {
        errno = EDEADLK;
        mutex_unlock(&stream_state_mutex);
        return;
    }

    f = malloc(sizeof(filter_t));
    assert(f != NULL);

    f->func = filtfunc;
    f->data = obj;
    TAILQ_INSERT_TAIL(&streams[hnd].filters, f, lent);
    mutex_unlock(&stream_state_mutex);
}

void snd_stream_filter_remove(snd_stream_hnd_t hnd, snd_stream_filter_t filtfunc, void * obj) {
    filter_t *f;

    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);

    if(streams[hnd].callback_active) {
        errno = EDEADLK;
        mutex_unlock(&stream_state_mutex);
        return;
    }

    TAILQ_FOREACH(f, &streams[hnd].filters, lent) {
        if(f->func == filtfunc && f->data == obj) {
            TAILQ_REMOVE(&streams[hnd].filters, f, lent);
            free(f);
            mutex_unlock(&stream_state_mutex);
            return;
        }
    }
    mutex_unlock(&stream_state_mutex);
}

static inline void process_filters(snd_stream_hnd_t hnd, void **buffer, int *samplecnt) {
    filter_t *f;

    TAILQ_FOREACH(f, &streams[hnd].filters, lent) {
        f->func(hnd, f->data, streams[hnd].frequency, streams[hnd].channels, buffer, samplecnt);
    }
}

void snd_pcm16_split_sq(uint32_t *data, uintptr_t left, uintptr_t right, size_t size) {
    uint32_t i;
    uint16_t *s = (uint16_t *)data;
    size_t remain = size;
    uint32_t *masked_left;
    uint32_t *masked_right;

    /* SPU memory in cached area */
    left |= SPU_RAM_BASE;
    right |= SPU_RAM_BASE;

    masked_left = SQ_MASK_DEST(left);
    masked_right = SQ_MASK_DEST(right);

    sq_lock((void *)left);
    dcache_pref_line(s);

    g2_lock_scoped();

    /* Make sure the FIFOs are empty */
    g2_fifo_wait();

    /* Separating channels and fill/write queues as many times as necessary. */
    for(; remain >= 128; remain -= 128) {

        /* Fill SQ0 */
        for(i = 0; i < 16; i += 2) {
            masked_left[i / 2] = (s[i * 2] << 16) | s[(i + 1) * 2];
        }

        /* Write-back SQ0 */
        sq_flush(masked_left);

        /* Fill SQ1 */
        for(i = 16; i < 32; i += 2) {
            masked_left[i / 2] = (s[i * 2] << 16) | s[(i + 1) * 2];
        }

        /* Write-back SQ1 */
        sq_flush(masked_left + 8);
        masked_left += 16;

        /* Fill SQ0 */
        for(i = 0; i < 16; i += 2) {
            masked_right[i / 2] = (s[(i * 2) + 1] << 16) | s[((i + 1) * 2) + 1];
        }

        /* Write-back SQ0 */
        sq_flush(masked_right);

        /* Fill SQ1 */
        for(i = 16; i < 32; i += 2) {
            masked_right[i / 2] = (s[(i * 2) + 1] << 16) | s[((i + 1) * 2) + 1];
        }

        /* Write-back SQ1 */
        sq_flush(masked_right + 8);
        masked_right += 16;
        s += 64;
    }

    sq_unlock();

    /* We can wait after unlock because G2 lock disables IRQ */
    sq_wait();

    if(remain) {
        left |= MEM_AREA_P2_BASE;
        right |= MEM_AREA_P2_BASE;
        left += size - remain;
        right += size - remain;

        for(; remain >= 4; remain -= 4) {
            *((volatile uint16_t *)left) = *s++;
            *((volatile uint16_t *)right) = *s++;
            left += 2;
            right += 2;
        }
    }
}

static void stream_channel_config_pack(snd_channel_config_t *destination,
                                       const snd_stream_config_t *source,
                                       unsigned int channel,
                                       uint32_t sample_address,
                                       uint32_t sample_count) {
    const snd_stream_channel_config_t *controls = &source->channel[channel];

    snd_channel_config_init(destination);
    destination->sample_address = sample_address;
    destination->format = (snd_channel_sample_format_t)source->format;
    destination->sample_count = sample_count;
    destination->loop_enabled = true;
    destination->loop_start = 0;
    destination->loop_end = sample_count;
    destination->sample_rate = source->sample_rate;
    destination->volume = controls->volume;
    destination->pan = controls->pan;
    destination->envelope = controls->envelope;
    destination->lfo = controls->lfo;
    destination->routing = controls->routing;
    destination->filter = controls->filter;
}

int snd_stream_config_init(snd_stream_config_t *config) {
    snd_channel_config_t channel;
    unsigned int i;

    if(!config) {
        errno = EINVAL;
        return -1;
    }

    memset(config, 0, sizeof(*config));
    snd_channel_config_init(&channel);
    config->format = SND_STREAM_FORMAT_PCM16;
    config->sample_rate = 44100;
    config->channels = 1;
    config->transfer_timeout_ms = SND_STREAM_TRANSFER_TIMEOUT_DEFAULT;

    for(i = 0; i < 2; ++i) {
        config->channel[i].volume = channel.volume;
        config->channel[i].pan = channel.pan;
        config->channel[i].envelope = channel.envelope;
        config->channel[i].lfo = channel.lfo;
        config->channel[i].routing = channel.routing;
        config->channel[i].filter = channel.filter;
    }

    return 0;
}

int snd_stream_config_validate(const snd_stream_config_t *config) {
    snd_channel_config_t channel;
    unsigned int i;

    if(!config ||
       (config->format != SND_STREAM_FORMAT_PCM16 &&
        config->format != SND_STREAM_FORMAT_PCM8 &&
        config->format != SND_STREAM_FORMAT_ADPCM) ||
       !config->sample_rate || config->sample_rate > (UINT32_MAX >> 10) ||
       (config->channels != 1 && config->channels != 2) ||
       !config->transfer_timeout_ms) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < config->channels; ++i) {
        stream_channel_config_pack(&channel, config, i, AICA_RAM_START, 2);
        if(snd_channel_config_validate(&channel) < 0)
            return -1;
    }

    return 0;
}

/* Initialize stream system */
int snd_stream_init(void) {
    return snd_stream_init_ex(2, SND_STREAM_BUFFER_MAX);
}

int snd_stream_init_ex(int channels, size_t buffer_size) {
    uint32_t *new_buffer = NULL;

    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if((channels != 1 && channels != 2) ||
       buffer_size > SND_STREAM_BUFFER_MAX_PCM16 ||
       (buffer_size && (buffer_size & 63))) {
        errno = EINVAL;
        mutex_unlock(&stream_state_mutex);
        return -1;
    }

    if(max_channels) {
        if(channels > max_channels) {
            dbglog(DBG_ERROR, "snd_stream_init_ex(): already initialized"
                " with %d channels, but %d requested\n",
                max_channels, channels);
            errno = EBUSY;
            mutex_unlock(&stream_state_mutex);
            return -1;
        }
        else if(buffer_size > max_buffer_size) {
            dbglog(DBG_ERROR, "snd_stream_init_ex(): already initialized"
                " with %zu buffer size, but %zu requested\n",
                max_buffer_size, buffer_size);
            errno = EBUSY;
            mutex_unlock(&stream_state_mutex);
            return -1;
        }
        mutex_unlock(&stream_state_mutex);
        return 0;
    }

    if(buffer_size > 0) {
        /* Create stereo separation buffers. This buffer size for each channel.
           But half size of streams buffer is enough, because stream
           polling doesn't read more than half buffer at time.
           This can also be used for mono streams on unaligned data.
        */
        new_buffer = aligned_alloc(32, buffer_size);

        if(new_buffer == NULL) {
            dbglog(DBG_ERROR, "snd_stream_init_ex(): memory allocation failed\n");
            mutex_unlock(&stream_state_mutex);
            return -1;
        }
    }

    /* Finish loading the stream driver */
    if(snd_init() < 0) {
        dbglog(DBG_ERROR, "snd_stream_init_ex(): snd_init() failed, giving up\n");
        free(new_buffer);
        mutex_unlock(&stream_state_mutex);
        return -1;
    }

    /* Publish the global limits only after every fallible initialization step
       has succeeded. Each separation buffer occupies half the allocation. */
    sep_buffer[0] = new_buffer;
    sep_buffer[1] = new_buffer ? new_buffer + (buffer_size / 8) : NULL;
    max_channels = channels;
    max_buffer_size = buffer_size;

    mutex_unlock(&stream_state_mutex);
    return 0;
}

snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb, int bufsize) {
    int i, saved_errno;
    snd_stream_hnd_t hnd = SND_STREAM_INVALID;
    uint32_t ram = 0;
    int ch0 = -1;
    int ch1 = -1;

    if(mutex_lock(&stream_state_mutex) < 0)
        return SND_STREAM_INVALID;
    if(!max_channels) {
        errno = ENODEV;
        mutex_unlock(&stream_state_mutex);
        return SND_STREAM_INVALID;
    }

    if(bufsize <= 0 || (bufsize & 31) ||
       (size_t)bufsize > SND_STREAM_BUFFER_MAX_PCM16 ||
       (max_buffer_size && (size_t)bufsize > max_buffer_size)) {
        errno = EINVAL;
        mutex_unlock(&stream_state_mutex);
        return SND_STREAM_INVALID;
    }

    if(sem_wait_timed(&stream_sem, SND_STREAM_TRANSFER_TIMEOUT_DEFAULT) < 0) {
        mutex_unlock(&stream_state_mutex);
        return SND_STREAM_INVALID;
    }

    /* Get an unused handle */
    for(i = 0; i < SND_STREAM_MAX; i++) {
        if(!streams[i].initted) {
            hnd = i;
            break;
        }
    }
    if(hnd == SND_STREAM_INVALID) {
        errno = ENOSPC;
        sem_signal(&stream_sem);
        mutex_unlock(&stream_state_mutex);
        return SND_STREAM_INVALID;
    }

    ram = snd_mem_malloc((size_t)bufsize * (size_t)max_channels);

    if(!ram)
        goto fail;

    ch0 = snd_sfx_chn_alloc();

    if(ch0 < 0) {
        errno = ENOSPC;
        goto fail;
    }

    if(max_channels == 2) {
        ch1 = snd_sfx_chn_alloc();

        if(ch1 < 0) {
            errno = ENOSPC;
            goto fail;
        }
    }

    /* A handle becomes visible only after all resources are owned. */
    memset(&streams[hnd], 0, sizeof(streams[hnd]));
    streams[hnd].ch[0] = ch0;
    streams[hnd].ch[1] = ch1;
    streams[hnd].buffer_size = (size_t)bufsize;
    streams[hnd].spu_ram_sch[0] = ram;
    streams[hnd].spu_ram_sch[1] = max_channels == 2 ? ram + (uint32_t)bufsize : 0;
    streams[hnd].get_data = cb;
    TAILQ_INIT(&streams[hnd].filters);
    snd_stream_config_init(&streams[hnd].config);
    streams[hnd].state = SND_STREAM_STATE_ALLOCATED;
    streams[hnd].initted = 1;

    sem_signal(&stream_sem);
    mutex_unlock(&stream_state_mutex);
    // dbglog(DBG_INFO, "snd_stream: alloc'd channels %d/%d\n", streams[hnd].ch[0], streams[hnd].ch[1]);
    return hnd;

fail:
    saved_errno = errno;

    if(ch1 >= 0)
        snd_sfx_chn_free(ch1);

    if(ch0 >= 0)
        snd_sfx_chn_free(ch0);

    if(ram)
        snd_mem_free(ram);

    sem_signal(&stream_sem);
    mutex_unlock(&stream_state_mutex);
    errno = saved_errno;
    return SND_STREAM_INVALID;
}

snd_stream_hnd_t snd_stream_reinit(snd_stream_hnd_t hnd, snd_stream_callback_t cb) {
    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);

    /* Start off with queueing disabled */
    streams[hnd].queueing = 0;

    /* Setup the callback */
    snd_stream_set_callback(hnd, cb);
    snd_stream_set_callback_direct(hnd, NULL);

    mutex_unlock(&stream_state_mutex);
    return hnd;
}

int snd_stream_destroy_ex(snd_stream_hnd_t hnd, uint32_t timeout_ms) {
    filter_t *c, *n;

    if(!timeout_ms) {
        errno = EINVAL;
        return -1;
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0)
        goto fail;
    if(streams[hnd].service_owner) {
        errno = EBUSY;
        goto fail;
    }

    if(snd_stream_stop_ex(hnd, timeout_ms) < 0)
        goto fail;
    snd_sfx_chn_free(streams[hnd].ch[0]);

    if(streams[hnd].ch[1] >= 0)
        snd_sfx_chn_free(streams[hnd].ch[1]);

    c = TAILQ_FIRST(&streams[hnd].filters);

    while(c) {
        n = TAILQ_NEXT(c, lent);
        free(c);
        c = n;
    }

    TAILQ_INIT(&streams[hnd].filters);

    snd_mem_free(streams[hnd].spu_ram_sch[0]);
    // dbglog(DBG_INFO, "snd_stream: dealloc'd channels %d/%d\n", streams[hnd].ch[0], streams[hnd].ch[1]);
    memset(streams + hnd, 0, sizeof(streams[0]));
    mutex_unlock(&stream_state_mutex);
    return 0;

fail:
    mutex_unlock(&stream_state_mutex);
    return -1;
}

void snd_stream_destroy(snd_stream_hnd_t hnd) {
    if(snd_stream_destroy_ex(hnd, SND_STREAM_TRANSFER_TIMEOUT_DEFAULT) < 0)
        dbglog(DBG_ERROR, "snd_stream_destroy(): %s\n", strerror(errno));
}

/* Shut everything down and free mem */
void snd_stream_shutdown(void) {
    /* Stop and destroy all active stream */
    int i;
    int active = 0;

    if(mutex_lock(&stream_state_mutex) < 0)
        return;
    for(i = 0; i < SND_STREAM_MAX; i++) {
        if(streams[i].initted) {
            snd_stream_destroy(i);
            active |= streams[i].initted;
        }
    }

    if(active) {
        mutex_unlock(&stream_state_mutex);
        return;
    }

    /* Free global buffers */
    if(sep_buffer[0]) {
        free(sep_buffer[0]);
        sep_buffer[0] = NULL;
        sep_buffer[1] = NULL;
    }

    max_channels = 0;
    max_buffer_size = 0;
    mutex_unlock(&stream_state_mutex);
}

/* Enable / disable stream queueing */
void snd_stream_queue_enable(snd_stream_hnd_t hnd) {
    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);
    streams[hnd].queueing = 1;
    mutex_unlock(&stream_state_mutex);
}

void snd_stream_queue_disable(snd_stream_hnd_t hnd) {
    mutex_lock(&stream_state_mutex);
    CHECK_HND(hnd);
    streams[hnd].queueing = 0;
    mutex_unlock(&stream_state_mutex);
}

static size_t stream_format_limit(snd_stream_sample_format_t format) {
    if(format == SND_STREAM_FORMAT_PCM16)
        return SND_STREAM_BUFFER_MAX_PCM16;
    if(format == SND_STREAM_FORMAT_PCM8)
        return SND_STREAM_BUFFER_MAX_PCM8;
    return SND_STREAM_BUFFER_MAX_ADPCM;
}

static void stream_reset_progress(strchan_t *stream) {
    stream->last_write_pos = 0;
    stream->last_play_pos = 0;
    stream->play_position = 0;
    stream->played_frames = 0;
    stream->source_bytes = 0;
    stream->buffered_bytes = 0;
    stream->polls = 0;
    stream->underruns = 0;
    stream->last_error = 0;
    stream->dma_error = 0;
}

int snd_stream_start_ex(snd_stream_hnd_t hnd,
                        const snd_stream_config_t *config) {
    snd_channel_config_t channel_config;
    strchan_t *stream;
    uint64_t sync_channels;
    size_t source_bytes;
    size_t format_limit;
    int fill_result;
    int had_underrun = 0;
    int result = -1;

    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0 || snd_stream_config_validate(config) < 0)
        goto out;

    stream = &streams[hnd];
    if(stream->callback_active) {
        errno = EDEADLK;
        goto out;
    }
    if(!stream->get_data && !stream->req_data) {
        errno = ENODATA;
        goto out;
    }
    if(config->channels > max_channels) {
        errno = ENOSPC;
        goto out;
    }
    if(!stream->req_data && stream->get_data && !sep_buffer[0]) {
        errno = ENOMEM;
        goto out;
    }
    if(stream->queue_gate_held || stream->state == SND_STREAM_STATE_QUEUED ||
       stream->state == SND_STREAM_STATE_PLAYING ||
        stream->state == SND_STREAM_STATE_UNDERRUN) {
        errno = EBUSY;
        goto out;
    }

    format_limit = stream_format_limit(config->format);
    stream->active_buffer_size = stream->buffer_size < format_limit ?
                                 stream->buffer_size : format_limit;
    stream->type = config->format;
    stream->bitsize = config->format == SND_STREAM_FORMAT_PCM16 ? 16 :
                      config->format == SND_STREAM_FORMAT_PCM8 ? 8 : 4;
    stream->channels = config->channels;
    stream->frequency = (int)config->sample_rate;
    stream->config = *config;
    stream_reset_progress(stream);

    fill_result = snd_stream_fill(hnd, 0, stream->active_buffer_size / 2,
                                  &source_bytes);
    if(fill_result < 0)
        goto fail;
    if(fill_result > 0) {
        ++stream->underruns;
        had_underrun = 1;
    }

    fill_result = snd_stream_fill(hnd, stream->active_buffer_size / 2,
                                  stream->active_buffer_size / 2,
                                  &source_bytes);
    if(fill_result < 0)
        goto fail;
    if(fill_result > 0) {
        ++stream->underruns;
        had_underrun = 1;
    }

    /* The second half may still be in flight. Key-on cannot race the DMA that
       initializes the ring it is about to consume. */
    if(stream_dma_wait(stream, config->transfer_timeout_ms) < 0)
        goto fail;

    snd_sh4_to_aica_stop();
    stream->queue_gate_held = 1;
    stream_channel_config_pack(&channel_config, config, 0,
                               stream->spu_ram_sch[0],
                               bytes_to_samples(hnd,
                                                stream->active_buffer_size));
    if(snd_channel_start(stream->ch[0], &channel_config,
                         SND_CHANNEL_START_DELAYED) < 0)
        goto queue_fail;

    sync_channels = UINT64_C(1) << stream->ch[0];
    if(stream->channels == 2) {
        stream_channel_config_pack(&channel_config, config, 1,
                                   stream->spu_ram_sch[1],
                                   bytes_to_samples(hnd,
                                                    stream->active_buffer_size));
        if(snd_channel_start(stream->ch[1], &channel_config,
                             SND_CHANNEL_START_DELAYED) < 0)
            goto queue_fail;
        sync_channels |= UINT64_C(1) << stream->ch[1];
    }

    if(stream->queueing) {
        stream->queued_start_channels = sync_channels;
        snd_sh4_to_aica_start();
        stream->queue_gate_held = 0;
        stream->state = SND_STREAM_STATE_QUEUED;
    }
    else {
        if(snd_channels_start_sync(sync_channels) < 0)
            goto queue_fail;
        snd_sh4_to_aica_start();
        stream->queue_gate_held = 0;
        stream->queued_start_channels = 0;
        stream->state = had_underrun ? SND_STREAM_STATE_UNDERRUN :
                                      SND_STREAM_STATE_PLAYING;
    }
    if(had_underrun) {
        stream->last_error = ENODATA;
    }
    result = 0;
    goto out;

queue_fail:
    {
        int saved_errno = errno;

        /* Release the queue gate even when admission fails. Delayed channels
           cannot key on without the synchronized-start packet. */
        snd_sh4_to_aica_start();
        stream->queue_gate_held = 0;
        stream->queued_start_channels = 0;
        errno = saved_errno;
    }
fail:
    stream_set_error(stream, errno);
out:
    mutex_unlock(&stream_state_mutex);
    return result;
}

/* Legacy starts retain their source surface while using the checked engine. */
static int snd_stream_start_type(snd_stream_hnd_t hnd,
                                 snd_stream_sample_format_t format,
                                 uint32_t freq, int stereo) {
    snd_stream_config_t config;

    if(snd_stream_config_init(&config) < 0)
        return -1;
    config.format = format;
    config.sample_rate = freq;
    config.channels = stereo ? 2 : 1;
    if(config.channels == 2) {
        config.channel[0].pan = 0;
        config.channel[1].pan = 255;
    }
    return snd_stream_start_ex(hnd, &config);
}

void snd_stream_start(snd_stream_hnd_t hnd, uint32_t freq, int st) {
    (void)snd_stream_start_type(hnd, SND_STREAM_FORMAT_PCM16, freq, st);
}

void snd_stream_start_pcm8(snd_stream_hnd_t hnd, uint32_t freq, int st) {
    (void)snd_stream_start_type(hnd, SND_STREAM_FORMAT_PCM8, freq, st);
}

void snd_stream_start_adpcm(snd_stream_hnd_t hnd, uint32_t freq, int st) {
    (void)snd_stream_start_type(hnd, SND_STREAM_FORMAT_ADPCM, freq, st);
}

int snd_stream_queue_go_ex(snd_stream_hnd_t hnd) {
    strchan_t *stream;
    int result = -1;

    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0)
        goto out;
    stream = &streams[hnd];
    if(stream->callback_active) {
        errno = EDEADLK;
        goto out;
    }
    if(stream->state != SND_STREAM_STATE_QUEUED ||
       !stream->queued_start_channels) {
        errno = EINVAL;
        goto out;
    }

    if(snd_channels_start_sync(stream->queued_start_channels) < 0) {
        stream_set_error(stream, errno);
        goto out;
    }
    stream->queued_start_channels = 0;
    stream->state = stream->underruns ? SND_STREAM_STATE_UNDERRUN :
                                       SND_STREAM_STATE_PLAYING;
    stream->last_error = stream->underruns ? ENODATA : 0;
    result = 0;

out:
    mutex_unlock(&stream_state_mutex);
    return result;
}

/* Preserve the original void entry point while routing through the checked
   queued-start implementation. */
void snd_stream_queue_go(snd_stream_hnd_t hnd) {
    (void)snd_stream_queue_go_ex(hnd);
}

static uint32_t stream_time_remaining(uint64_t deadline) {
    uint64_t now = timer_ms_gettime64();
    uint64_t remaining;

    if(now >= deadline)
        return 0;
    remaining = deadline - now;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

static int stream_wait_channels_stopped(const strchan_t *stream,
                                        uint64_t deadline) {
    snd_channel_status_ex_t status;
    int channel;

    for(;;) {
        bool playing = false;

        for(channel = 0; channel < stream->channels; ++channel) {
            if(snd_channel_get_status_ex(stream->ch[channel], &status) < 0)
                return -1;
            playing |= status.playing;
        }

        if(!playing)
            return 0;
        if(!stream_time_remaining(deadline)) {
            errno = ETIMEDOUT;
            return -1;
        }
        thd_pass();
    }
}

int snd_stream_stop_ex(snd_stream_hnd_t hnd, uint32_t timeout_ms) {
    snd_driver_status_t driver_status;
    strchan_t *stream;
    uint64_t deadline;
    int saved_errno = 0;
    int channel;
    int result = -1;

    if(!timeout_ms) {
        errno = EINVAL;
        return -1;
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0)
        goto out;

    stream = &streams[hnd];
    if(stream->callback_active) {
        errno = EDEADLK;
        goto out;
    }
    if(!stream->channels) {
        stream->state = SND_STREAM_STATE_STOPPED;
        stream->last_error = 0;
        result = 0;
        goto out;
    }

    deadline = timer_ms_gettime64() + timeout_ms;
    if(!stream->queue_gate_held) {
        snd_sh4_to_aica_stop();
        stream->queue_gate_held = 1;
    }

    for(channel = 0; channel < stream->channels; ++channel) {
        if(snd_channel_stop(stream->ch[channel]) < 0 && !saved_errno)
            saved_errno = errno;
    }

    /* Always release a queue gate acquired by start or stop, including on a
       partial command-admission failure. */
    snd_sh4_to_aica_start();
    stream->queue_gate_held = 0;
    stream->queued_start_channels = 0;
    if(saved_errno) {
        errno = saved_errno;
        goto fail;
    }

    /* A channel may already report silent before its queued stop is consumed.
       The status query is a FIFO command barrier: its reply proves every stop
       above has left the command ring before channel ownership can be freed. */
    {
        uint32_t remaining = stream_time_remaining(deadline);

        if(!remaining) {
            errno = ETIMEDOUT;
            goto fail;
        }
        if(snd_driver_get_status(&driver_status, remaining) < 0)
            goto fail;
    }

    {
        uint32_t remaining = stream_time_remaining(deadline);

        if(!remaining) {
            errno = ETIMEDOUT;
            goto fail;
        }
        if(stream->dma_pending && stream_dma_wait(stream, remaining) < 0)
            goto fail;
        stream->dma_error = 0;
    }

    if(stream_wait_channels_stopped(stream, deadline) < 0)
        goto fail;

    stream->state = SND_STREAM_STATE_STOPPED;
    stream->last_error = 0;
    result = 0;
    goto out;

fail:
    stream_set_error(stream, errno);
out:
    mutex_unlock(&stream_state_mutex);
    return result;
}

/* Stop streaming through the checked path with the compatibility deadline. */
void snd_stream_stop(snd_stream_hnd_t hnd) {
    (void)snd_stream_stop_ex(hnd, SND_STREAM_TRANSFER_TIMEOUT_DEFAULT);
}

static void stream_dma_finish(strchan_t *stream, int error) {
    irq_t old_irq = irq_disable();

    if(error) {
        stream_set_error(stream, error);
    }
    else {
        stream->source_bytes += stream->dma_source_bytes;
        stream->buffered_bytes += stream->dma_buffered_bytes;
    }

    stream->dma_source_bytes = 0;
    stream->dma_buffered_bytes = 0;
    stream->dma_error = error;
    stream->dma_pending = 0;
    irq_restore(old_irq);
    sem_signal(&stream_sem);
}

/* The DMA will chain to this to start the second DMA. */
static inline void dma_done(void *data) {
    stream_dma_finish((strchan_t *)data, 0);
}

static inline void dma_chain(void *data) {
    strchan_t *stream = (strchan_t *)data;
    int rs = spu_dma_transfer(sep_buffer[1], stream->dma_dest,
                              stream->dma_length, 0, dma_done, data);

    if(rs < 0)
        stream_dma_finish(stream, EIO);
}

static int stream_dma_wait(strchan_t *stream, uint32_t timeout_ms) {
    if(!stream->dma_pending) {
        if(stream->dma_error) {
            errno = stream->dma_error;
            return -1;
        }
        return 0;
    }

    if(sem_wait_timed(&stream_sem, timeout_ms) < 0) {
        int saved_errno = errno;
        stream_set_error(stream, saved_errno);
        errno = saved_errno;
        return -1;
    }

    sem_signal(&stream_sem);
    if(stream->dma_pending) {
        stream_set_error(stream, EIO);
        errno = EIO;
        return -1;
    }
    if(stream->dma_error) {
        errno = stream->dma_error;
        return -1;
    }

    return 0;
}

/* stream_sem is held on entry. DMA completion retains ownership until its
   callback publishes counters and releases the semaphore. Short or unaligned
   writes use the exact PIO path and release ownership before returning. */
static int snd_stream_transfer(strchan_t *stream, void *first_buf,
                               uint32_t offset, size_t size,
                               uint64_t source_bytes,
                               uint64_t buffered_bytes) {
    int rs;

    if(!__is_aligned(first_buf, 32) || (size & 31)) {
        spu_memload_sq(stream->spu_ram_sch[0] + offset, first_buf, size);

        if(stream->channels == 2)
            spu_memload_sq(stream->spu_ram_sch[1] + offset, sep_buffer[1], size);

        stream->source_bytes += source_bytes;
        stream->buffered_bytes += buffered_bytes;
        sem_signal(&stream_sem);
        return 0;
    }

    dcache_purge_range((uintptr_t)first_buf, size);
    if(stream->channels == 2) {
        dcache_purge_range((uintptr_t)sep_buffer[1], size);
        stream->dma_dest = stream->spu_ram_sch[1] + offset;
        stream->dma_length = size;
    }

    stream->dma_source_bytes = source_bytes;
    stream->dma_buffered_bytes = buffered_bytes;
    stream->dma_error = 0;
    stream->dma_pending = 1;

    do {
        rs = spu_dma_transfer(first_buf, stream->spu_ram_sch[0] + offset,
                              size, 0,
                              stream->channels == 1 ? dma_done : dma_chain,
                              stream);

        if(rs == 0)
            return 0;
        if(errno != EINPROGRESS)
            break;
        thd_pass();
    } while(1);

    stream->dma_pending = 0;
    stream->dma_error = errno ? errno : EIO;
    stream->dma_source_bytes = 0;
    stream->dma_buffered_bytes = 0;
    sem_signal(&stream_sem);
    return -1;
}

/* Fill one per-channel interval. A successful call always initializes the
   whole interval; callback shortfall becomes silence rather than stale ring
   data. source_bytes reports the callback-owned portion to the caller. */
static int snd_stream_fill(snd_stream_hnd_t hnd, uint32_t offset, size_t size,
                           size_t *source_bytes) {
    strchan_t *stream = &streams[hnd];
    const int chans = stream->channels;
    const uintptr_t left = stream->spu_ram_sch[0] + offset;
    const uintptr_t right = stream->spu_ram_sch[1] + offset;
    const size_t needed_bytes = size * (size_t)chans;
    const size_t frame_size = callback_frame_size(stream);
    size_t got_bytes = 0;
    int callback_bytes = 0;
    void *data = NULL;

    CHECK_HND(hnd);
    assert(chans != 0);
    *source_bytes = 0;

    /* A previous DMA may still consume the driver-owned separation buffer.
       Drain it before asking a callback for data that could reuse its source. */
    if(stream_dma_wait(stream, stream->config.transfer_timeout_ms) < 0)
        return -1;

    if(stream->req_data) {
        if(sem_wait_timed(&stream_sem,
                          stream->config.transfer_timeout_ms) < 0)
            return -1;

        stream->callback_active = 1;
        got_bytes = stream->req_data(hnd, left | SPU_RAM_UNCACHED_BASE,
                         chans == 2 ? right | SPU_RAM_UNCACHED_BASE : 0,
                         needed_bytes);
        stream->callback_active = 0;
        if(got_bytes == SIZE_MAX) {
            int saved_errno = errno ? errno : EIO;
            sem_signal(&stream_sem);
            errno = saved_errno;
            return -1;
        }
        if(got_bytes > needed_bytes ||
           got_bytes % (stream->bitsize == 4 ? (size_t)chans : frame_size)) {
            sem_signal(&stream_sem);
            errno = EOVERFLOW;
            return -1;
        }

        if(got_bytes < needed_bytes) {
            size_t per_channel = got_bytes / (size_t)chans;

            spu_memset_sq(left + per_channel, 0, size - per_channel);
            if(chans == 2)
                spu_memset_sq(right + per_channel, 0, size - per_channel);
        }

        stream->source_bytes += got_bytes;
        stream->buffered_bytes += needed_bytes;
        sem_signal(&stream_sem);
        *source_bytes = got_bytes;
        return got_bytes == needed_bytes ? 0 : 1;
    }

    if(stream->get_data) {
        stream->callback_active = 1;
        data = stream->get_data(hnd, (int)needed_bytes, &callback_bytes);
    }

    if(callback_bytes < 0 || (data == NULL && callback_bytes != 0)) {
        stream->callback_active = 0;
        errno = EIO;
        return -1;
    }

    got_bytes = (size_t)callback_bytes;
    if(got_bytes > needed_bytes) {
        stream->callback_active = 0;
        errno = EOVERFLOW;
        return -1;
    }

    if(data && got_bytes)
        process_filters(hnd, &data, &callback_bytes);
    stream->callback_active = 0;

    if(callback_bytes < 0 || (size_t)callback_bytes > needed_bytes ||
       ((size_t)callback_bytes % frame_size) ||
       (!data && callback_bytes)) {
        errno = EOVERFLOW;
        return -1;
    }
    got_bytes = (size_t)callback_bytes;

    if(sem_wait_timed(&stream_sem, stream->config.transfer_timeout_ms) < 0)
        return -1;

    if(chans == 1) {
        if(!sep_buffer[0]) {
            sem_signal(&stream_sem);
            errno = ENOMEM;
            return -1;
        }

        memset(sep_buffer[0], 0, size);
        if(got_bytes)
            memcpy(sep_buffer[0], data, got_bytes);
        if(snd_stream_transfer(stream, sep_buffer[0], offset, size, got_bytes,
                               needed_bytes) < 0)
            return -1;
    }
    else {
        memset(sep_buffer[0], 0, size);
        memset(sep_buffer[1], 0, size);

        if(got_bytes) {
            if(!(got_bytes & 31) && __is_aligned(data, 32)) {
                if(stream->bitsize == 16)
                    snd_pcm16_split(data, sep_buffer[0], sep_buffer[1],
                                    got_bytes);
                else if(stream->bitsize == 8)
                    snd_pcm8_split(data, sep_buffer[0], sep_buffer[1],
                                   got_bytes);
                else
                    snd_adpcm_split(data, sep_buffer[0], sep_buffer[1],
                                    got_bytes);
            }
            else {
                snd_stream_split_exact(stream->bitsize, data, sep_buffer[0],
                                       sep_buffer[1], got_bytes);
            }
        }

        if(snd_stream_transfer(stream, sep_buffer[0], offset, size, got_bytes,
                               needed_bytes) < 0)
            return -1;
    }

    *source_bytes = got_bytes;
    return got_bytes == needed_bytes ? 0 : 1;
}

static int stream_refresh_playback(strchan_t *stream,
                                   bool playing[2]) {
    snd_channel_status_ex_t channel_status;
    irq_t old_irq;
    uint32_t buffer_samples;
    uint32_t current = 0;
    uint32_t delta;
    int channel;

    for(channel = 0; channel < stream->channels; ++channel) {
        if(snd_channel_get_status_ex(stream->ch[channel],
                                     &channel_status) < 0)
            return -1;
        playing[channel] = channel_status.playing;
        if(channel == 0)
            current = channel_status.position;
    }
    if(stream->channels == 1)
        playing[1] = false;

    buffer_samples = (uint32_t)bytes_to_samples(
        (snd_stream_hnd_t)(stream - streams), stream->active_buffer_size);
    if(!buffer_samples || current >= buffer_samples) {
        errno = EPROTO;
        return -1;
    }

    old_irq = irq_disable();
    if(stream->state == SND_STREAM_STATE_PLAYING ||
       stream->state == SND_STREAM_STATE_UNDERRUN) {
        delta = current >= stream->last_play_pos ?
                current - stream->last_play_pos :
                buffer_samples - stream->last_play_pos + current;
        stream->played_frames += delta;
    }
    stream->last_play_pos = current;
    stream->play_position = current;
    irq_restore(old_irq);
    return 0;
}

static int snd_stream_poll_internal(snd_stream_hnd_t hnd, bool legacy,
                                    const void *service_owner,
                                    bool *underrun) {
    uint32_t write_pos;
    uint32_t current_play_pos;
    int needed_samples = 0;
    size_t needed_bytes = 0;
    size_t source_bytes = 0;
    bool playing[2];
    int fill_result;
    int result = -1;
    strchan_t *stream;

    if(underrun)
        *underrun = false;

    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0)
        goto out;
    stream = &streams[hnd];

    if(stream->service_owner != service_owner) {
        errno = EBUSY;
        goto out;
    }

    /* A service can own a stream before it starts or while it is queued. It
       defers those states without changing the stream's lifecycle to ERROR. */
    if(service_owner && stream->state != SND_STREAM_STATE_PLAYING &&
       stream->state != SND_STREAM_STATE_UNDERRUN) {
        errno = EAGAIN;
        goto out;
    }

    if(stream->callback_active) {
        errno = EDEADLK;
        goto out;
    }

    if(!stream->get_data && !stream->req_data) {
        errno = ENODATA;
        goto fail;
    }
    if(!stream->channels || stream->state == SND_STREAM_STATE_ALLOCATED ||
       stream->state == SND_STREAM_STATE_STOPPED) {
        errno = EINVAL;
        goto fail;
    }
    if(stream->queue_gate_held || stream->state == SND_STREAM_STATE_QUEUED) {
        errno = EAGAIN;
        goto out;
    }

    /* Get channels position */
    if(stream_refresh_playback(stream, playing) < 0)
        goto fail;
    current_play_pos = stream->play_position;
    stream->polls++;

    needed_bytes = samples_to_bytes(hnd, current_play_pos);

    if(needed_bytes >= stream->active_buffer_size) {
        dbglog(DBG_ERROR, "snd_stream_poll: chan0(%d).pos = %lu\n",
               stream->ch[0], (unsigned long)current_play_pos);
        errno = EPROTO;
        goto fail;
    }

    if(needed_bytes & 31) {
        /* Aligning for DMA. */
        current_play_pos &= ~(bytes_to_samples(hnd, 32) - 1);
    }

    /* Count just till the end of the buffer, so we don't have to
       handle buffer wraps */
    if(stream->last_write_pos <= current_play_pos) {
        needed_samples = current_play_pos - stream->last_write_pos - 1;
        /* Round it to max sector size of supported storage devices */
        needed_samples &= ~(bytes_to_samples(hnd, 2048 / stream->channels) - 1);
        needed_bytes = samples_to_bytes(hnd, needed_samples);
        /* Reduce data requests */
        if(needed_bytes < (stream->active_buffer_size / 2)) {
            result = 0;
            goto out;
        }
    }
    else {
        needed_samples = bytes_to_samples(hnd, stream->active_buffer_size);
        needed_samples -= stream->last_write_pos;
        needed_bytes = samples_to_bytes(hnd, needed_samples);
    }

    if(needed_samples <= 0) {
        result = 0;
        goto out;
    }

    if(needed_bytes > stream->active_buffer_size / 2)
        needed_bytes = stream->active_buffer_size / 2;

    write_pos = samples_to_bytes(hnd, stream->last_write_pos);
    fill_result = snd_stream_fill(hnd, write_pos, needed_bytes,
                                  &source_bytes);
    if(fill_result < 0)
        goto fail;

    /* The whole destination interval is initialized even when source data is
       short, so the producer advances over silence exactly as hardware does. */
    needed_samples = bytes_to_samples(hnd, needed_bytes);

    stream->last_write_pos += needed_samples;
    write_pos = (uint32_t)bytes_to_samples(hnd, stream->active_buffer_size);

    if(stream->last_write_pos >= write_pos)
        stream->last_write_pos -= write_pos;

    if(fill_result > 0) {
        if(underrun)
            *underrun = true;
        stream->underruns++;
        stream->state = SND_STREAM_STATE_UNDERRUN;
        stream->last_error = ENODATA;
        errno = ENODATA;
        result = legacy ? -3 : -1;
        goto out;
    }

    stream->state = SND_STREAM_STATE_PLAYING;
    stream->last_error = 0;
    result = 0;
    goto out;

fail:
    stream_set_error(stream, errno);
out:
    mutex_unlock(&stream_state_mutex);
    return result;
}

/* Poll streamer to load more data if necessary. */
int snd_stream_poll(snd_stream_hnd_t hnd) {
    return snd_stream_poll_internal(hnd, true, NULL, NULL);
}

int snd_stream_poll_ex(snd_stream_hnd_t hnd) {
    return snd_stream_poll_internal(hnd, false, NULL, NULL);
}

int _snd_stream_service_claim(snd_stream_hnd_t hnd, const void *owner) {
    int result = -1;

    if(!owner) {
        errno = EINVAL;
        return -1;
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0)
        goto out;
    if(streams[hnd].service_owner) {
        errno = EBUSY;
        goto out;
    }

    streams[hnd].service_owner = owner;
    result = 0;

out:
    mutex_unlock(&stream_state_mutex);
    return result;
}

int _snd_stream_service_release(snd_stream_hnd_t hnd, const void *owner) {
    int result = -1;

    if(!owner) {
        errno = EINVAL;
        return -1;
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0)
        goto out;
    if(streams[hnd].service_owner != owner) {
        errno = EPERM;
        goto out;
    }

    streams[hnd].service_owner = NULL;
    result = 0;

out:
    mutex_unlock(&stream_state_mutex);
    return result;
}

int _snd_stream_service_poll(snd_stream_hnd_t hnd, const void *owner,
                             bool *underrun) {
    if(!owner || !underrun) {
        errno = EINVAL;
        return -1;
    }

    return snd_stream_poll_internal(hnd, false, owner, underrun);
}

static void stream_copy_update_fields(snd_stream_config_t *destination,
                                      const snd_stream_config_t *source,
                                      uint32_t fields) {
    int channel;

    if(fields & SND_CHANNEL_UPDATE_FREQUENCY)
        destination->sample_rate = source->sample_rate;

    for(channel = 0; channel < destination->channels; ++channel) {
        if(fields & SND_CHANNEL_UPDATE_VOLUME)
            destination->channel[channel].volume =
                source->channel[channel].volume;
        if(fields & SND_CHANNEL_UPDATE_PAN)
            destination->channel[channel].pan = source->channel[channel].pan;
        if(fields & SND_CHANNEL_UPDATE_ENVELOPE)
            destination->channel[channel].envelope =
                source->channel[channel].envelope;
        if(fields & SND_CHANNEL_UPDATE_LFO)
            destination->channel[channel].lfo = source->channel[channel].lfo;
        if(fields & SND_CHANNEL_UPDATE_ROUTING)
            destination->channel[channel].routing =
                source->channel[channel].routing;
        if(fields & SND_CHANNEL_UPDATE_FILTER)
            destination->channel[channel].filter =
                source->channel[channel].filter;
    }
}

int snd_stream_update(snd_stream_hnd_t hnd,
                      const snd_stream_config_t *config, uint32_t fields) {
    snd_channel_config_t channel_config;
    strchan_t *stream;
    bool acquired_gate = false;
    int saved_errno = 0;
    int channel;
    int result = -1;

    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0 || snd_stream_config_validate(config) < 0)
        goto out;

    stream = &streams[hnd];
    if(stream->callback_active) {
        errno = EDEADLK;
        goto out;
    }
    if(!fields || (fields & ~SND_CHANNEL_UPDATE_ALL) || !stream->channels ||
       config->format != stream->config.format ||
       config->channels != stream->config.channels ||
       stream->state == SND_STREAM_STATE_ALLOCATED ||
       stream->state == SND_STREAM_STATE_STOPPED) {
        errno = EINVAL;
        goto out;
    }

    if(!stream->queue_gate_held) {
        snd_sh4_to_aica_stop();
        stream->queue_gate_held = 1;
        acquired_gate = true;
    }

    for(channel = 0; channel < stream->channels; ++channel) {
        stream_channel_config_pack(
            &channel_config, config, (unsigned int)channel,
            stream->spu_ram_sch[channel],
            bytes_to_samples(hnd, stream->active_buffer_size));
        if(snd_channel_update(stream->ch[channel], &channel_config,
                              fields) < 0) {
            saved_errno = errno;
            break;
        }
    }

    if(acquired_gate) {
        snd_sh4_to_aica_start();
        stream->queue_gate_held = 0;
    }
    if(saved_errno) {
        errno = saved_errno;
        goto fail;
    }

    stream_copy_update_fields(&stream->config, config, fields);
    stream->frequency = (int)stream->config.sample_rate;
    stream->last_error = 0;
    result = 0;
    goto out;

fail:
    stream_set_error(stream, errno);
out:
    mutex_unlock(&stream_state_mutex);
    return result;
}

int snd_stream_get_status(snd_stream_hnd_t hnd,
                          snd_stream_status_t *status) {
    strchan_t *stream;
    bool playing[2] = {false, false};
    irq_t old_irq;
    int result = -1;

    if(!status) {
        errno = EINVAL;
        return -1;
    }
    memset(status, 0, sizeof(*status));
    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    if(checked_handle(hnd) < 0)
        goto out;

    stream = &streams[hnd];
    if(stream->channels && stream_refresh_playback(stream, playing) < 0)
        goto out;

    old_irq = irq_disable();
    status->state = stream->state;
    status->last_error = stream->last_error;
    status->format = stream->config.format;
    status->sample_rate = stream->config.sample_rate;
    status->channels = stream->channels;
    status->queueing = stream->queueing != 0;
    status->dma_pending = stream->dma_pending != 0;
    status->buffer_size = stream->active_buffer_size;
    status->play_position = stream->play_position;
    status->write_position = stream->last_write_pos;
    status->source_bytes = stream->source_bytes;
    status->buffered_bytes = stream->buffered_bytes;
    status->played_bytes = interleaved_bytes_for_frames(
        stream->bitsize, stream->played_frames, stream->channels);
    status->polls = stream->polls;
    status->underruns = stream->underruns;
    status->channel[0] = stream->ch[0];
    status->channel[1] = stream->ch[1];
    status->channel_playing[0] = playing[0];
    status->channel_playing[1] = playing[1];
    status->service_owned = stream->service_owner != NULL;
    irq_restore(old_irq);
    result = 0;

out:
    mutex_unlock(&stream_state_mutex);
    return result;
}

/* Set the volume on the streaming channels */
void snd_stream_volume(snd_stream_hnd_t hnd, int vol) {
    snd_stream_config_t config;

    if(vol < 0 || vol > 255) {
        errno = EINVAL;
        return;
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return;
    if(checked_handle(hnd) < 0) {
        mutex_unlock(&stream_state_mutex);
        return;
    }
    config = streams[hnd].config;
    config.channel[0].volume = (uint8_t)vol;
    config.channel[1].volume = (uint8_t)vol;
    (void)snd_stream_update(hnd, &config, SND_CHANNEL_UPDATE_VOLUME);
    mutex_unlock(&stream_state_mutex);
}

/* Set the panning on the streaming channels */
void snd_stream_pan(snd_stream_hnd_t hnd, int left_pan, int right_pan) {
    snd_stream_config_t config;

    if(left_pan < 0 || left_pan > 255 || right_pan < 0 || right_pan > 255) {
        errno = EINVAL;
        return;
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return;
    if(checked_handle(hnd) < 0) {
        mutex_unlock(&stream_state_mutex);
        return;
    }
    config = streams[hnd].config;
    config.channel[0].pan = (uint8_t)left_pan;
    config.channel[1].pan = (uint8_t)right_pan;
    (void)snd_stream_update(hnd, &config, SND_CHANNEL_UPDATE_PAN);
    mutex_unlock(&stream_state_mutex);
}
