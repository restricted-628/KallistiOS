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
#include "snd_iface_internal.h"
#include "snd_retained_pcm.h"
#include "snd_retained_gain.h"
#include "snd_retained_memory.h"
#include "snd_retained_start_tl.h"
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
                               uint64_t buffered_bytes, uint64_t deadline) {
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
        if(deadline && timer_ms_gettime64() >= deadline) {
            errno = ETIMEDOUT;
            break;
        }
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
                               needed_bytes, 0) < 0)
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
                               needed_bytes, 0) < 0)
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

/* Retained PCM uses the existing stream allocator, PCM splitter and two-stage
   DMA. Its handle is private and permanently pinned once start is attempted. */
#define RETAINED_SPANS (4 * SND_RETAINED_PCM_FILL_SPANS)
#define RETAINED_OBSERVE_MS 100u
#define RETAINED_GAIN_STEP_MS 20u

typedef struct retained_span {
    snd_retained_pcm_span_t tag;
    uint64_t end;
} retained_span_t;

typedef struct retained_source {
    uint64_t generation, written, played, next_frame, last_end;
    bool sealed;
} retained_source_t;

typedef struct retained_cursor {
    uint64_t frames, before_ms, after_ms;
} retained_cursor_t;

struct snd_retained_pcm {
    snd_stream_hnd_t handle;
    uint32_t session, ring, rate;
    unsigned channels;
    uint64_t epoch, key_on_not_before, written, output;
    snd_retained_pcm_fill_t fill;
    void *context;
    int16_t *scratch;
    const int16_t *memory_data;
    uint32_t memory_frames;
    bool memory, memory_repeat;
    bool attempted, stopping, stopped, valid;
    int first_error, stop_error;
    unsigned source_count, first, count;
    unsigned stop_submitted, stop_observed;
    snd_retained_gain_status_t gain;
    bool gain_rotating;
    retained_source_t sources[SND_RETAINED_PCM_GENERATIONS];
    retained_span_t spans[RETAINED_SPANS];
    retained_cursor_t cursor[2];
    snd_channel_receipt_t receipt[2];
    bool receipt_polled[2];
};

static uint64_t retained_epoch;

static retained_source_t *retained_source(retained_source_t *sources,
    unsigned count, uint64_t generation) {
    for(unsigned i = 0; i < count; ++i)
        if(sources[i].generation == generation)
            return &sources[i];
    return NULL;
}

static int retained_memory_fill(void *context, int16_t *data, uint32_t frames,
    snd_retained_pcm_span_t *spans, unsigned capacity) {
    snd_retained_pcm_t *pcm = context;
    uint64_t written = pcm->source_count ? pcm->sources[0].written : 0;
    uint32_t offset = (uint32_t)(pcm->memory_repeat ? written % pcm->memory_frames : written);
    uint32_t copied = 0;
    int count = 0;
    (void)capacity; /* A fill has at most one rewind or one silence tail. */

    while(copied < frames) {
        uint32_t take = pcm->memory_frames - offset;
        if(!take) {
            spans[count++] = (snd_retained_pcm_span_t){
                0, 0, frames - copied, SND_RETAINED_PCM_TERMINAL_SILENCE
            };
            break;
        }
        if(take > frames - copied)
            take = frames - copied;
        memcpy(data + copied * pcm->channels,
               pcm->memory_data + (size_t)offset * pcm->channels,
               take * pcm->channels * sizeof(*data));
        spans[count++] = (snd_retained_pcm_span_t){
            SND_RETAINED_MEMORY_GENERATION, offset, take, SND_RETAINED_PCM_SOURCE
        };
        copied += take;
        offset += take;
        if(pcm->memory_repeat && offset == pcm->memory_frames)
            offset = 0;
    }
    return count;
}

static int retained_error(snd_retained_pcm_t *pcm) {
    if(!pcm->first_error)
        pcm->first_error = errno ? errno : EIO;
    pcm->valid = false;
    errno = pcm->first_error;
    return -1;
}

int snd_retained_pcm_open(snd_retained_pcm_t **out, unsigned channels,
    uint32_t rate, uint32_t ring, snd_retained_pcm_fill_t fill, void *context) {
    if(!out || !fill || channels < 1 || channels > 2 || rate < 8000 ||
       rate > 48000 || ring < 1024 || ring > 32768 || ring % 32) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    snd_retained_pcm_t *pcm = calloc(1, sizeof(*pcm));
    if(!pcm)
        return -1;
    pcm->scratch = aligned_alloc(32, ring * channels);
    if(!pcm->scratch)
        goto free_pcm;
    if(mutex_lock(&stream_state_mutex) < 0)
        goto free_pcm;
    if(channels > (unsigned)max_channels) {
        errno = max_channels ? ENOSPC : ENODEV;
        goto unlock;
    }
    if(!sep_buffer[0]) {
        errno = ENOTSUP;
        goto unlock;
    }
    if(retained_epoch == UINT64_MAX) {
        errno = EOVERFLOW;
        goto unlock;
    }
    if(snd_observation_bind_until(timer_ms_gettime64() + RETAINED_OBSERVE_MS,
                                  &pcm->session) < 0)
        goto unlock;
    pcm->handle = snd_stream_alloc(NULL, (int)(ring * 2));
    if(pcm->handle == SND_STREAM_INVALID)
        goto unlock;
    streams[pcm->handle].service_owner = pcm;
    pcm->epoch = ++retained_epoch;
    pcm->ring = ring;
    pcm->rate = rate;
    pcm->channels = channels;
    pcm->fill = fill;
    pcm->context = context;
    *out = pcm;
    mutex_unlock(&stream_state_mutex);
    return 0;

unlock:
    mutex_unlock(&stream_state_mutex);
free_pcm:
    free(pcm->scratch);
    free(pcm);
    return -1;
}

int snd_retained_pcm_open_memory(snd_retained_pcm_t **out, unsigned channels,
    uint32_t rate, uint32_t ring) {
    if(snd_retained_pcm_open(out, channels, rate, ring, retained_memory_fill, NULL) < 0)
        return -1;
    (*out)->memory = true;
    (*out)->context = *out;
    return 0;
}

int snd_retained_pcm_memory_bind(snd_retained_pcm_t *pcm, const int16_t *data,
    uint32_t frames, bool repeat) {
    if(!pcm || !pcm->memory || !data || !frames ||
       frames > SIZE_MAX / pcm->channels / sizeof(*data) ||
       (repeat && frames < pcm->ring / 2)) {
        errno = EINVAL;
        return -1;
    }
    if(pcm->attempted || pcm->stopping) {
        errno = EBUSY;
        return -1;
    }
    pcm->memory_data = data;
    pcm->memory_frames = frames;
    pcm->memory_repeat = repeat;
    return 0;
}

int snd_retained_pcm_memory_position(const snd_retained_pcm_t *pcm,
    snd_retained_memory_position_t *position) {
    if(!pcm || !pcm->memory || !position) {
        errno = EINVAL;
        return -1;
    }
    if(!pcm->memory_frames) {
        errno = ENODATA;
        return -1;
    }
    uint64_t written = pcm->source_count ? pcm->sources[0].written : 0;
    *position = (snd_retained_memory_position_t){
        .submitted_frames = written,
        .next_frame = (uint32_t)(pcm->memory_repeat ? written % pcm->memory_frames : written),
    };
    return 0;
}

static int retained_session(const snd_retained_pcm_t *pcm, uint64_t deadline) {
    uint32_t session;
    if(snd_observation_bind_until(deadline, &session) < 0)
        return -1;
    if(session != pcm->session) {
        errno = ENODEV;
        return -1;
    }
    return 0;
}

/* Conservative service bound for the fixed-rate slice. The margin covers
   pitch quantization and host millisecond rounding. This bounds accepted
   cursor intervals, not analog output timing. */
static uint64_t retained_movement(const snd_retained_pcm_t *pcm, uint64_t ms) {
    if(ms > 4096)
        return UINT64_MAX;
    return (pcm->rate + pcm->rate / 100u + 1) * (ms + 1) / 1000 + 2;
}

enum retained_sample_phase {
    RETAINED_SAMPLE_NORMAL,
    RETAINED_SAMPLE_GAIN_OLD,
    RETAINED_SAMPLE_GAIN_NEW,
};

static int retained_sample_until(snd_retained_pcm_t *pcm, retained_cursor_t *next,
    uint64_t *output, uint64_t deadline, enum retained_sample_phase phase) {
    bool gain = phase == RETAINED_SAMPLE_GAIN_NEW;
    for(unsigned i = 0; i < pcm->channels; ++i) {
        snd_channel_observation_t obs;
        const retained_cursor_t *prev = &pcm->cursor[i];

        if(snd_observation_poll_until(&pcm->receipt[i], deadline, &obs) < 0)
            return -1;
        pcm->receipt_polled[i] = true;
        if(obs.session != pcm->session || obs.token != pcm->receipt[i].token ||
           obs.channel != pcm->receipt[i].channel ||
           obs.operation != SND_OBSERVATION_UPDATE) {
            errno = EPROTO;
            return -1;
        }
        if(obs.result != SND_OBSERVATION_APPLIED) {
            if(gain) {
                unsigned bit = 1u << i;
                pcm->gain.rejected |= bit;
                pcm->gain.unknown &= ~bit;
            }
            errno = EPROTO;
            return -1;
        }
        /* The immutable result classifies the command independently of
           whether the live channel sample can still qualify the clock. */
        if(gain) {
            unsigned bit = 1u << i;
            pcm->gain.applied |= bit;
            pcm->gain.unknown &= ~bit;
            pcm->gain.acknowledged |= bit;
            pcm->gain.acknowledged_tl[i] = pcm->gain.requested_tl[i];
        }
        if(obs.attribution_kind != SND_OBSERVATION_ATTR_MANAGED ||
           obs.attribution_token != obs.token || obs.key_on != 1 ||
           obs.position >= pcm->ring ||
           obs.host_challenge_started_ms < prev->after_ms ||
           obs.host_observed_ms < obs.host_challenge_started_ms ||
           obs.host_observed_ms < prev->before_ms) {
            errno = EPROTO;
            return -1;
        }

        uint64_t bound = retained_movement(pcm, obs.host_observed_ms - prev->before_ms);
        uint32_t prior = (uint32_t)(prev->frames % pcm->ring);
        uint32_t delta = (obs.position + pcm->ring - prior) % pcm->ring;
        if(bound >= pcm->ring || delta > bound || prev->frames > UINT64_MAX - delta) {
            errno = EOVERFLOW;
            return -1;
        }
        next[i] = (retained_cursor_t){ prev->frames + delta,
            obs.host_challenge_started_ms, obs.host_observed_ms };
    }

    uint64_t latest = next[pcm->channels - 1].after_ms;
    if(pcm->channels == 2 && next[1].before_ms < next[0].after_ms) {
        errno = EPROTO;
        return -1;
    }
    for(unsigned i = 0; i < pcm->channels; ++i) {
        uint64_t possible = retained_movement(pcm, latest - next[i].before_ms);
        /* It must still be possible to prove the entire interval was backed
           by the old committed ring. Even a late successful transfer cannot
           hide stale replay at the previous write frontier. */
        bool before_frontier = next[i].frames < pcm->written;
        if(before_frontier && possible >= pcm->written - next[i].frames) {
            /* A qualified prior sample also bounds the same slot at latest.
               Stereo gain rotates receipts using only TL updates, leaving
               the slot and pitch unchanged even across pending retries.
               The new samples alone still determine credit. */
            const retained_cursor_t *prev = &pcm->cursor[i];
            before_frontier = (pcm->channels == 1 || phase != RETAINED_SAMPLE_NORMAL) &&
                pcm->valid &&
                prev->frames < pcm->written &&
                retained_movement(pcm, latest - prev->before_ms) < pcm->written - prev->frames;
        }
        if(!before_frontier) {
            errno = EPIPE;
            return -1;
        }
    }

    *output = next[0].frames;
    if(pcm->channels == 2) {
        /* Synchronized key-on and equal fixed pitch define one timeline.
           The right challenge starts after the left observation ends.
           Elapsed time can advance the later cursor, never move it back.
           There is no qualified reverse tolerance for this profile. */
        uint64_t left = next[0].frames;
        uint64_t right = next[1].frames;
        if(right < left ||
           right - left > retained_movement(pcm, latest - next[0].before_ms)) {
            errno = EPROTO;
            return -1;
        }
    }
    return 0;
}

static int retained_sample(snd_retained_pcm_t *pcm, retained_cursor_t *next,
                           uint64_t *output) {
    return retained_sample_until(pcm, next, output,
        timer_ms_gettime64() + RETAINED_OBSERVE_MS, RETAINED_SAMPLE_NORMAL);
}

static void retained_credit(snd_retained_pcm_t *pcm, const retained_cursor_t *next,
                            uint64_t output) {
    while(pcm->count && pcm->output < output) {
        const retained_span_t *span = &pcm->spans[pcm->first];
        uint64_t end = output < span->end ? output : span->end;
        if(span->tag.kind == SND_RETAINED_PCM_SOURCE) {
            retained_source_t *source = retained_source(pcm->sources,
                pcm->source_count, span->tag.generation);
            source->played += end - pcm->output;
            source->next_frame = span->tag.first_frame +
                (end - (span->end - span->tag.frames));
        }
        pcm->output = end;
        if(end == span->end) {
            pcm->first = (pcm->first + 1) % RETAINED_SPANS;
            --pcm->count;
        }
    }
    memcpy(pcm->cursor, next, pcm->channels * sizeof(*next));
    pcm->valid = true;
}

static int retained_prepare(snd_retained_pcm_t *pcm, snd_retained_pcm_span_t *tags,
    retained_source_t *sources, unsigned *source_count) {
    uint32_t frames = pcm->ring / 2;
    uint32_t covered = 0;
    unsigned count = pcm->source_count;

    memcpy(sources, pcm->sources, sizeof(pcm->sources));
    int spans = pcm->fill(pcm->context, pcm->scratch, frames, tags,
                         SND_RETAINED_PCM_FILL_SPANS);
    if(spans < 0)
        return -1;
    if(!spans || spans > (int)SND_RETAINED_PCM_FILL_SPANS)
        goto malformed;

    for(int i = 0; i < spans; ++i) {
        const snd_retained_pcm_span_t *tag = &tags[i];
        if(!tag->frames || tag->frames > frames - covered)
            goto malformed;
        if(tag->kind == SND_RETAINED_PCM_SOURCE) {
            if(!tag->generation || tag->first_frame > UINT64_MAX - tag->frames)
                goto malformed;
            if(!count || tag->generation > sources[count - 1].generation) {
                if(count == SND_RETAINED_PCM_GENERATIONS) {
                    errno = ENOSPC;
                    return -1;
                }
                sources[count++].generation = tag->generation;
            }
            retained_source_t *source = &sources[count - 1];
            if(source->generation != tag->generation || source->sealed ||
               source->written > UINT64_MAX - tag->frames)
                goto malformed;
            source->written += tag->frames;
            source->last_end = pcm->written + covered + tag->frames;
        }
        else {
            if(tag->kind < SND_RETAINED_PCM_STARVATION ||
               tag->kind > SND_RETAINED_PCM_TERMINAL_SILENCE ||
               tag->generation || tag->first_frame)
                goto malformed;
            memset(pcm->scratch + covered * pcm->channels, 0,
                   tag->frames * pcm->channels * sizeof(*pcm->scratch));
        }
        covered += tag->frames;
    }

    if(covered != frames)
        goto malformed;
    *source_count = count;
    return spans;

malformed:
    errno = EPROTO;
    return -1;
}

static int retained_write(snd_retained_pcm_t *pcm, bool running) {
    snd_retained_pcm_span_t tags[SND_RETAINED_PCM_FILL_SPANS] = {0};
    retained_source_t sources[SND_RETAINED_PCM_GENERATIONS];
    unsigned source_count;
    strchan_t *stream = &streams[pcm->handle];

    if(pcm->count > RETAINED_SPANS - SND_RETAINED_PCM_FILL_SPANS) {
        errno = EAGAIN;
        return -1;
    }
    if(pcm->written > UINT64_MAX - pcm->ring / 2) {
        errno = EOVERFLOW;
        return retained_error(pcm);
    }

    int spans = retained_prepare(pcm, tags, sources, &source_count);
    if(spans < 0)
        return retained_error(pcm);

    /* Check again after calling the producer. Even if the producer blocked,
       do not start a DMA after the old write frontier became uncertain. */
    retained_cursor_t next[2];
    uint64_t output;
    if(running) {
        if(retained_sample(pcm, next, &output) < 0)
            return retained_error(pcm);
        retained_credit(pcm, next, output);
        /* Credit changed while the shadow write transaction was being prepared. */
        for(unsigned i = 0; i < pcm->source_count; ++i) {
            sources[i].played = pcm->sources[i].played;
            sources[i].next_frame = pcm->sources[i].next_frame;
        }
    }

    uint32_t timeout = stream->config.transfer_timeout_ms;
    if(sem_wait_timed(&stream_sem, timeout) < 0)
        return retained_error(pcm);
    size_t bytes = pcm->ring; /* One half, two bytes per channel sample. */
    if(pcm->channels == 2)
        snd_pcm16_split((void *)pcm->scratch, sep_buffer[0], sep_buffer[1], bytes * 2);
    else
        memcpy(sep_buffer[0], pcm->scratch, bytes);
    uint32_t offset = (uint32_t)(pcm->written % pcm->ring) * 2;
    if(snd_stream_transfer(stream, sep_buffer[0], offset, bytes, bytes * pcm->channels,
                           bytes * pcm->channels, timer_ms_gettime64() + timeout) < 0 ||
       stream_dma_wait(stream, timeout) < 0)
        return retained_error(pcm);

    if(running) {
        if(retained_sample(pcm, next, &output) < 0)
            return retained_error(pcm);
        retained_credit(pcm, next, output);
        for(unsigned i = 0; i < pcm->source_count; ++i) {
            sources[i].played = pcm->sources[i].played;
            sources[i].next_frame = pcm->sources[i].next_frame;
        }
    }

    if(pcm->memory && !pcm->memory_repeat && source_count &&
       sources[0].written == pcm->memory_frames)
        sources[0].sealed = true;
    memcpy(pcm->sources, sources, sizeof(pcm->sources));
    pcm->source_count = source_count;
    for(int i = 0; i < spans; ++i) {
        unsigned index = (pcm->first + pcm->count++) % RETAINED_SPANS;
        pcm->written += tags[i].frames;
        pcm->spans[index] = (retained_span_t){ tags[i], pcm->written };
    }
    return 0;
}

static int retained_consume(snd_retained_pcm_t *pcm, unsigned i, uint64_t deadline) {
    if(!pcm->receipt[i].token)
        return 0;
    if(!pcm->receipt_polled[i]) {
        snd_channel_observation_t obs;
        if(snd_observation_poll_until(&pcm->receipt[i], deadline, &obs) < 0)
            return -1;
        pcm->receipt_polled[i] = true;
    }
    if(snd_observation_consume_until(&pcm->receipt[i], deadline) < 0)
        return -1;
    pcm->receipt[i] = (snd_channel_receipt_t){0};
    pcm->receipt_polled[i] = false;
    return 0;
}

static int retained_confirm_key_off(snd_retained_pcm_t *pcm, unsigned i,
    uint32_t operation, uint64_t deadline) {
    const snd_channel_receipt_t *receipt = &pcm->receipt[i];
    snd_channel_observation_t obs;
    if(snd_observation_poll_until(receipt, deadline, &obs) < 0)
        return -1;
    pcm->receipt_polled[i] = true;
    if(obs.session != pcm->session || obs.token != receipt->token ||
       obs.channel != receipt->channel || obs.operation != operation ||
       obs.result != SND_OBSERVATION_APPLIED ||
       obs.attribution_kind != SND_OBSERVATION_ATTR_MANAGED ||
       obs.attribution_token != receipt->token || obs.key_on) {
        errno = EPROTO;
        return -1;
    }
    return retained_consume(pcm, i, deadline);
}

static int retained_key_on(snd_retained_pcm_t *pcm, strchan_t *stream,
    const unsigned *initial_tl) {
    snd_channel_config_t config[2];
    uint64_t mask = 0;

    for(unsigned i = 0; i < pcm->channels; ++i) {
        unsigned channel = (unsigned)stream->ch[i];
        uint64_t deadline = timer_ms_gettime64() + RETAINED_OBSERVE_MS;
        stream_channel_config_pack(&config[i], &stream->config, i,
                                   stream->spu_ram_sch[i], pcm->ring);
        if(snd_observation_submit_until(channel, SND_OBSERVATION_START, &config[i],
                0, SND_CHANNEL_START_DELAYED, &pcm->receipt[i], deadline) < 0 ||
           retained_confirm_key_off(pcm, i, SND_OBSERVATION_START, deadline) < 0)
            return -1;
        if(initial_tl) {
            deadline = timer_ms_gettime64() + RETAINED_OBSERVE_MS;
            if(snd_observation_submit_tl_until(channel, initial_tl[i],
                    &pcm->receipt[i], deadline) < 0 ||
               retained_confirm_key_off(pcm, i, SND_OBSERVATION_UPDATE, deadline) < 0)
                return -1;
        }
        mask |= UINT64_C(1) << channel;
    }

    /* Measure from the earliest possible key-on. SYNC marks the slots legacy.
       An UPDATE with the same linear volume or raw TL establishes the managed
       anchor without changing the level, position or paired start. */
    pcm->key_on_not_before = timer_ms_gettime64();
    if(snd_channels_start_sync(mask) < 0)
        return -1;
    for(unsigned i = 0; i < pcm->channels; ++i) {
        pcm->cursor[i].before_ms = pcm->key_on_not_before;
        pcm->cursor[i].after_ms = pcm->key_on_not_before;
        unsigned channel = (unsigned)stream->ch[i];
        uint64_t deadline = timer_ms_gettime64() + RETAINED_OBSERVE_MS;
        int result = initial_tl ?
            snd_observation_submit_tl_until(channel, initial_tl[i], &pcm->receipt[i], deadline) :
            snd_observation_submit_until(channel, SND_OBSERVATION_UPDATE, &config[i],
                SND_CHANNEL_UPDATE_VOLUME, 0, &pcm->receipt[i], deadline);
        if(result < 0)
            return -1;
    }
    return 0;
}

static int retained_start(snd_retained_pcm_t *pcm, const unsigned *initial_tl) {
    if(!pcm || pcm->attempted || pcm->stopping || (pcm->memory && !pcm->memory_data)) {
        errno = EINVAL;
        return -1;
    }
    if(initial_tl) {
        if(irq_inside_int()) {
            errno = EPERM;
            return -1;
        }
        if(!(snd_iface_observation_features() & AICA_DRIVER_FEATURE_OBSERVATION_TL)) {
            errno = ENOTSUP;
            return -1;
        }
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;

    pcm->attempted = true;
    strchan_t *stream = &streams[pcm->handle];
    snd_stream_config_init(&stream->config);
    stream->channels = stream->config.channels = pcm->channels;
    stream->config.sample_rate = pcm->rate;
    if(pcm->channels == 2) {
        stream->config.channel[0].pan = 0;
        stream->config.channel[1].pan = 255;
    }

    int result = -1;
    retained_cursor_t next[2];
    uint64_t output;
    if(retained_session(pcm, timer_ms_gettime64() + RETAINED_OBSERVE_MS) == 0 &&
       retained_write(pcm, false) == 0 &&
       retained_write(pcm, false) == 0 && retained_key_on(pcm, stream, initial_tl) == 0 &&
       retained_sample(pcm, next, &output) == 0) {
        retained_credit(pcm, next, output);
        for(unsigned i = 0; i < pcm->channels; ++i) {
            pcm->gain.acknowledged |= 1u << i;
            if(initial_tl)
                pcm->gain.acknowledged_tl[i] = (uint8_t)initial_tl[i];
        }
        result = 0;
    }
    else
        retained_error(pcm);
    mutex_unlock(&stream_state_mutex);
    return result;
}

int snd_retained_pcm_start(snd_retained_pcm_t *pcm) {
    return retained_start(pcm, NULL);
}

int snd_retained_pcm_start_with_tl(snd_retained_pcm_t *pcm, unsigned left_tl,
    unsigned right_tl) {
    if(left_tl > UINT8_MAX || right_tl > UINT8_MAX) {
        errno = EINVAL;
        return -1;
    }
    const unsigned initial_tl[2] = {left_tl, right_tl};
    return retained_start(pcm, initial_tl);
}

static bool retained_gain_pending(const snd_retained_pcm_t *pcm) {
    return pcm->gain.request && pcm->gain.state == SND_RETAINED_GAIN_PENDING;
}

/* From a previously qualified sample, do not cross a possible wrap or the
   old committed ring frontier while the active observation anchors rotate. */
static bool retained_gain_room(const snd_retained_pcm_t *pcm, uint64_t at) {
    for(unsigned i = 0; i < pcm->channels; ++i) {
        const retained_cursor_t *prev = &pcm->cursor[i];
        if(at < prev->after_ms || prev->frames >= pcm->written)
            return false;
        uint64_t advance = retained_movement(pcm, at - prev->before_ms);
        if(advance >= pcm->ring || advance >= pcm->written - prev->frames)
            return false;
    }
    return true;
}

int snd_retained_pcm_gain_begin(snd_retained_pcm_t *pcm, unsigned left,
    unsigned right, uint64_t *request) {
    if(!pcm || !request || left > UINT8_MAX || right > UINT8_MAX) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!(snd_iface_observation_features() & AICA_DRIVER_FEATURE_OBSERVATION_TL)) {
        errno = ENOTSUP;
        return -1;
    }
    if(retained_gain_pending(pcm)) {
        errno = EBUSY;
        return -1;
    }
    if(!pcm->attempted || !pcm->valid || pcm->stopping || pcm->first_error) {
        errno = EINVAL;
        return -1;
    }
    if(pcm->gain.request == UINT64_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    uint64_t now = timer_ms_gettime64();
    if(!retained_gain_room(pcm, now + 1)) {
        errno = EAGAIN;
        return -1;
    }

    ++pcm->gain.request;
    pcm->gain.epoch = pcm->epoch;
    pcm->gain.session = pcm->session;
    pcm->gain.state = SND_RETAINED_GAIN_PENDING;
    pcm->gain.admitted = pcm->gain.applied = pcm->gain.rejected = pcm->gain.unknown = 0;
    pcm->gain.requested_tl[0] = (uint8_t)left;
    pcm->gain.requested_tl[1] = pcm->channels == 2 ? (uint8_t)right : 0;
    pcm->gain.error = 0;
    pcm->gain_rotating = false;
    *request = pcm->gain.request;
    return 0;
}

int snd_retained_pcm_gain_status(const snd_retained_pcm_t *pcm, uint64_t request,
    snd_retained_gain_status_t *out) {
    if(!pcm || !request || !out) {
        errno = EINVAL;
        return -1;
    }
    if(request != pcm->gain.request) {
        errno = ENOENT;
        return -1;
    }
    *out = pcm->gain;
    return 0;
}

/* The lease serializes gain and refill. Advance with the same tokens after a
   timeout. The old cursor stays intact until all active anchors qualify. */
static int retained_gain_step(snd_retained_pcm_t *pcm, strchan_t *stream) {
    uint64_t now = timer_ms_gettime64();
    uint64_t deadline = now + RETAINED_GAIN_STEP_MS;
    while(deadline > now && !retained_gain_room(pcm, deadline))
        --deadline;
    if(deadline == now) {
        errno = ETIMEDOUT;
        goto failed;
    }

    retained_cursor_t next[2];
    uint64_t output;
    if(!pcm->gain_rotating) {
        if(retained_sample_until(pcm, next, &output, deadline, RETAINED_SAMPLE_GAIN_OLD) < 0)
            goto failed;
        retained_credit(pcm, next, output);
        pcm->gain_rotating = true;
    }

    for(unsigned i = 0; i < pcm->channels; ++i) {
        unsigned bit = 1u << i;
        if(pcm->gain.admitted & bit)
            continue;
        if(retained_consume(pcm, i, deadline) < 0 ||
           snd_observation_submit_tl_until((unsigned)stream->ch[i],
                    pcm->gain.requested_tl[i], &pcm->receipt[i], deadline) < 0)
            goto failed;
        pcm->gain.admitted |= bit;
        pcm->gain.unknown |= bit;
    }
    if(retained_sample_until(pcm, next, &output, deadline, RETAINED_SAMPLE_GAIN_NEW) < 0)
        goto failed;

    retained_credit(pcm, next, output);
    pcm->gain.state = SND_RETAINED_GAIN_APPLIED;
    pcm->gain.error = 0;
    return 1;

failed:
    pcm->gain.error = errno ? errno : EIO;
    if((errno == EAGAIN || errno == ETIMEDOUT || errno == EBUSY) &&
       retained_gain_room(pcm, timer_ms_gettime64() + 1))
        return 0;

    pcm->gain.state = SND_RETAINED_GAIN_FAILED;
    return retained_error(pcm);
}

int snd_retained_pcm_poll(snd_retained_pcm_t *pcm) {
    if(!pcm || !pcm->attempted || pcm->stopping) {
        errno = EINVAL;
        return -1;
    }
    if(pcm->first_error) {
        errno = pcm->first_error;
        return -1;
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    int result;
    if(retained_gain_pending(pcm)) {
        result = retained_gain_step(pcm, &streams[pcm->handle]);
        if(result <= 0)
            goto done;
    }
    else {
        retained_cursor_t next[2];
        uint64_t output;
        if(retained_sample(pcm, next, &output) < 0) {
            result = retained_error(pcm);
            goto done;
        }
        retained_credit(pcm, next, output);
    }
    /* Leave the current sample and the immediately preceding frame alone at
       the reuse boundary. Write a complete old half. */
    result = 0;
    if(pcm->written - pcm->output <= pcm->ring / 2 - 2)
        result = retained_write(pcm, true);

done:
    mutex_unlock(&stream_state_mutex);
    return result;
}

int snd_retained_pcm_seal(snd_retained_pcm_t *pcm, uint64_t generation, uint64_t total) {
    if(!pcm || !generation || pcm->memory || pcm->stopping || pcm->first_error) {
        errno = EINVAL;
        return -1;
    }
    retained_source_t *source = retained_source(pcm->sources, pcm->source_count,
                                                generation);
    if(!source && !total && (!pcm->source_count ||
       generation > pcm->sources[pcm->source_count - 1].generation)) {
        if(pcm->source_count == SND_RETAINED_PCM_GENERATIONS) {
            errno = ENOSPC;
            return -1;
        }
        source = &pcm->sources[pcm->source_count++];
        source->generation = generation;
        source->last_end = pcm->output;
    }
    if(!source || source->sealed || source->written != total) {
        errno = EINVAL;
        return -1;
    }
    source->sealed = true;
    return 0;
}

int snd_retained_pcm_query(const snd_retained_pcm_t *pcm, uint64_t generation,
                           snd_retained_pcm_snapshot_t *out) {
    if(!pcm || !out) {
        errno = EINVAL;
        return -1;
    }
    const retained_source_t *source = NULL;
    if(generation) {
        for(unsigned i = 0; i < pcm->source_count; ++i)
            if(pcm->sources[i].generation == generation)
                source = &pcm->sources[i];
        if(!source) {
            errno = ENOENT;
            return -1;
        }
    }
    uint64_t now = timer_ms_gettime64();
    uint64_t before = pcm->cursor[0].before_ms;
    bool fresh = now >= before &&
        retained_movement(pcm, now - before) < pcm->ring;
    bool rotating = retained_gain_pending(pcm);
    bool valid = pcm->valid && fresh && !rotating;
    *out = (snd_retained_pcm_snapshot_t){
        .epoch = pcm->epoch, .session = pcm->session,
        .output_frames = pcm->output, .generation = generation,
        .source_frames = source ? source->played : 0,
        .next_source_frame = source ? source->next_frame : 0,
        .observation_start_ms = before,
        .observation_end_ms = pcm->cursor[pcm->channels - 1].after_ms,
        .clock_error = valid ? 0 : pcm->first_error ? pcm->first_error :
            pcm->stopping ? ECANCELED : rotating ? EAGAIN :
            pcm->valid ? ETIMEDOUT : EAGAIN,
        .first_error = pcm->first_error, .stop_error = pcm->stop_error,
        .valid = valid, .software_stopped = pcm->stopped,
        .source_output_end = valid && source && source->sealed &&
            source->played == source->written && source->last_end <= pcm->output
    };
    return 0;
}

/* A FIFO barrier after a raw STOP cannot establish that ARM applied it.
   Retain each checked request and its observation across deadline failures. */
static int retained_stop_channels(snd_retained_pcm_t *pcm, strchan_t *stream,
                                  uint64_t deadline) {
    for(unsigned i = 0; i < pcm->channels; ++i) {
        unsigned bit = 1u << i;
        if(pcm->stop_submitted & bit)
            continue;
        if(retained_consume(pcm, i, deadline) < 0 ||
           snd_observation_submit_until((unsigned)stream->ch[i], SND_OBSERVATION_STOP,
                    NULL, 0, 0, &pcm->receipt[i], deadline) < 0)
            return -1;
        pcm->stop_submitted |= bit;
    }

    for(unsigned i = 0; i < pcm->channels; ++i) {
        unsigned bit = 1u << i;
        while(!(pcm->stop_observed & bit)) {
            snd_channel_observation_t obs;
            if(snd_observation_poll_until(&pcm->receipt[i], deadline, &obs) < 0)
                return -1;
            pcm->receipt_polled[i] = true;
            if(obs.session != pcm->session || obs.token != pcm->receipt[i].token ||
               obs.channel != (unsigned)stream->ch[i] ||
               obs.operation != SND_OBSERVATION_STOP ||
               obs.result != SND_OBSERVATION_APPLIED ||
               obs.attribution_kind != SND_OBSERVATION_ATTR_MANAGED ||
               obs.attribution_token != obs.token) {
                errno = EPROTO;
                return -1;
            }
            if(!obs.key_on)
                pcm->stop_observed |= bit;
        }
        if(retained_consume(pcm, i, deadline) < 0)
            return -1;
    }
    return 0;
}

static int retained_stop_transport(snd_retained_pcm_t *pcm, uint64_t deadline) {
    uint32_t remaining = stream_time_remaining(deadline);
    if(!remaining) {
        errno = ETIMEDOUT;
        return -1;
    }
    if(mutex_lock_timed(&stream_state_mutex, remaining) < 0)
        return -1;

    strchan_t *stream = &streams[pcm->handle];
    int result = -1;
    if(retained_session(pcm, deadline) < 0 ||
       retained_stop_channels(pcm, stream, deadline) < 0)
        goto done;

    remaining = stream_time_remaining(deadline);
    if(!remaining) {
        errno = ETIMEDOUT;
        goto done;
    }
    if(stream->dma_pending && stream_dma_wait(stream, remaining) < 0)
        goto done;

    /* A previously reported transfer error remains in first_error. The
       pinned lease has no remaining transfer or command bookkeeping. */
    stream->dma_error = 0;
    stream->last_error = 0;
    stream->state = SND_STREAM_STATE_STOPPED;
    result = 0;
done:
    {
        int saved = errno;
        mutex_unlock(&stream_state_mutex);
        errno = saved;
    }
    return result;
}

int snd_retained_pcm_stop(snd_retained_pcm_t *pcm, uint32_t timeout) {
    if(!pcm || !timeout) {
        errno = EINVAL;
        return -1;
    }
    if(pcm->stopped)
        return 0;

    if(retained_gain_pending(pcm)) {
        pcm->gain.state = SND_RETAINED_GAIN_FAILED;
        pcm->gain.error = ECANCELED;
    }
    pcm->stopping = true;
    pcm->valid = false;
    uint64_t deadline = timer_ms_gettime64() + timeout;
    if(pcm->attempted && retained_stop_transport(pcm, deadline) < 0)
        goto failed;
    free(pcm->scratch);
    pcm->scratch = NULL;
    pcm->fill = NULL;
    pcm->context = NULL;
    pcm->memory_data = NULL;
    pcm->stopped = true;
    pcm->stop_error = 0;
    return 0;

failed:
    pcm->stop_error = errno ? errno : EIO;
    errno = pcm->stop_error;
    return -1;
}

int snd_retained_pcm_close(snd_retained_pcm_t *pcm) {
    if(!pcm || pcm->attempted) {
        errno = pcm ? EBUSY : EINVAL;
        return -1;
    }
    if(mutex_lock(&stream_state_mutex) < 0)
        return -1;
    strchan_t *stream = &streams[pcm->handle];
    stream->service_owner = NULL;
    int result = snd_stream_destroy_ex(pcm->handle, SND_STREAM_TRANSFER_TIMEOUT_DEFAULT);
    if(result == 0) {
        free(pcm->scratch);
        free(pcm);
    }
    else
        stream->service_owner = pcm;
    mutex_unlock(&stream_state_mutex);
    return result;
}
