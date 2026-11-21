#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "snd_retained_pcm.h"
#include "snd_retained_gain.h"
#include "snd_retained_start_tl.h"
#include <dc/sound/aica_observation.h>

typedef int snd_stream_hnd_t;
typedef int irq_t;
typedef struct { unsigned pan; } test_controls_t;
typedef struct {
    unsigned channels, sample_rate, transfer_timeout_ms;
    test_controls_t channel[2];
} snd_stream_config_t;
typedef struct { unsigned unused; } snd_channel_config_t;
typedef struct { uint32_t session, token, channel; } snd_channel_receipt_t;
typedef struct {
    uint32_t session, token, channel, operation, result;
    uint32_t attribution_token, attribution_kind, position, key_on;
    uint64_t host_challenge_started_ms, host_observed_ms;
} snd_channel_observation_t;

enum {
    SND_STREAM_INVALID = -1, SND_OBSERVATION_START = 1, SND_OBSERVATION_STOP = 2, SND_OBSERVATION_UPDATE = 3,
    SND_OBSERVATION_APPLIED = 1, SND_OBSERVATION_ATTR_MANAGED = 1,
    SND_CHANNEL_UPDATE_VOLUME = 1, SND_CHANNEL_START_DELAYED = 1,
    SND_STREAM_TRANSFER_TIMEOUT_DEFAULT = 100, SND_STREAM_STATE_STOPPED
};

typedef struct {
    int channels, ch[2], dma_pending, dma_error, last_error, state;
    uint64_t dma_source_bytes, dma_buffered_bytes, source_bytes, buffered_bytes;
    uintptr_t dma_dest, spu_ram_sch[2];
    size_t dma_length;
    const void *service_owner;
    snd_stream_config_t config;
} strchan_t;

static int stream_state_mutex, stream_sem = 1, max_channels = 2;
static strchan_t streams[1];
static _Alignas(32) uint32_t separation[2][1024];
static uint32_t *sep_buffer[2] = { separation[0], separation[1] };
static uint64_t now, first_delay, right_dma_delay, right_observation_delay;
static uint64_t left_dma_delay, left_observation_delay;
static uint32_t delayed_position;
static uint32_t position[2], session = 1, token;
static int bad_channel = -1, fail_dma_channel = -1, stop_errno;
static bool retire_callback;
static int held_stop_channel = -1, timeout_ack_channel = -1;
static unsigned stop_submissions[2];
static unsigned dmas, frees, syncs;
static uint8_t sound_ram[2][4096];
static uint32_t operations[2];
static uint32_t gain_tokens[2], gain_features;
static unsigned gain_submissions[2], gain_tl[2];
static unsigned linear_anchors, key_off_tl_acks, tl_at_sync[2];
static int gain_submit_error, gain_reject_channel, gain_stale_channel;
static int gain_timeout_channel, gain_unattributed_channel;
static int delayed_start_key_on, delayed_tl_key_on;
static bool in_irq;

static bool irq_inside_int(void) { return in_irq; }
static int irq_disable(void) { return 0; }
static void irq_restore(int old) { (void)old; }
static int mutex_lock(int *lock) { ++*lock; return 0; }
static int mutex_lock_timed(int *lock, uint32_t timeout) {
    assert(timeout);
    return mutex_lock(lock);
}
static void mutex_unlock(int *lock) { assert((*lock)-- > 0); }
static uint64_t timer_ms_gettime64(void) { return now; }
static void thd_pass(void) { ++now; }
static bool __is_aligned(const void *p, unsigned alignment) {
    return (uintptr_t)p % alignment == 0;
}
static int sem_wait_timed(int *sem, uint32_t timeout) {
    assert(timeout);
    if(*sem != 1) {
        errno = ETIMEDOUT;
        return -1;
    }
    --*sem;
    return 0;
}
static void sem_signal(int *sem) { assert(!(*sem)++); }
static void dcache_purge_range(uintptr_t p, size_t n) { (void)p; (void)n; }
static void spu_memload_sq(uintptr_t dst, const void *p, size_t n) {
    assert(dst >= 4096 && dst + n <= 3 * 4096);
    memcpy(sound_ram[(dst / 4096) - 1] + dst % 4096, p, n);
}
static int spu_dma_transfer(const void *p, uintptr_t dst, size_t n, int block,
                            void (*done)(void *), void *context) {
    unsigned channel = dst / 4096 - 1;
    assert(!block && channel < 2 && __is_aligned(p, 32));
    ++dmas;
    if((int)channel == fail_dma_channel) {
        errno = EIO;
        return -1;
    }
    spu_memload_sq(dst, p, n);
    uint64_t dma_delay = channel == 0 ? left_dma_delay : right_dma_delay;
    if(dma_delay) {
        now += dma_delay;
        position[0] = position[1] = delayed_position;
    }
    if(retire_callback)
        done(context);
    return 0;
}
static void stream_set_error(strchan_t *stream, int error) { stream->dma_error = error; }

static int snd_stream_alloc(void *callback, int size) {
    assert(!callback && size <= 4096 && !streams[0].service_owner);
    streams[0] = (strchan_t){ .ch = {0, 1}, .spu_ram_sch = {4096, 8192} };
    return 0;
}
static int snd_stream_destroy_ex(int handle, uint32_t timeout) {
    assert(handle == 0 && timeout && !streams[0].service_owner);
    ++frees;
    return 0;
}
static uint32_t stream_time_remaining(uint64_t deadline) {
    return now < deadline ? (uint32_t)(deadline - now) : 0;
}
static void snd_stream_config_init(snd_stream_config_t *config) {
    *config = (snd_stream_config_t){ .transfer_timeout_ms = 100 };
}
static void stream_channel_config_pack(snd_channel_config_t *config,
    const snd_stream_config_t *source, unsigned channel, uint32_t address,
    uint32_t samples) {
    assert(channel < source->channels && address && samples);
    *config = (snd_channel_config_t){0};
}
static void snd_pcm16_split(const void *pcm, void *left, void *right, size_t bytes) {
    for(size_t i = 0; i < bytes / 4; ++i) {
        ((int16_t *)left)[i] = ((const int16_t *)pcm)[2 * i];
        ((int16_t *)right)[i] = ((const int16_t *)pcm)[2 * i + 1];
    }
}

static int snd_observation_bind_until(uint64_t deadline, uint32_t *out) {
    assert(deadline >= now);
    *out = session;
    return 0;
}
static int snd_observation_submit_until(unsigned channel, uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, uint32_t flags,
    snd_channel_receipt_t *receipt, uint64_t deadline) {
    assert(channel < (unsigned)streams[0].channels && deadline > now);
    if(operation == SND_OBSERVATION_STOP) {
        assert(!config && !flags && !fields);
        ++stop_submissions[channel];
    }
    else {
        assert(config);
        assert(operation == SND_OBSERVATION_START ?
               flags == SND_CHANNEL_START_DELAYED && !fields :
               !flags && fields == SND_CHANNEL_UPDATE_VOLUME && syncs);
        if(operation == SND_OBSERVATION_UPDATE) {
            ++linear_anchors;
            gain_tl[channel] = 0;
        }
    }
    operations[channel] = operation;
    *receipt = (snd_channel_receipt_t){session, ++token, channel};
    return 0;
}
static uint32_t snd_iface_observation_features(void) {
    return gain_features;
}
static int snd_observation_submit_tl_until(unsigned channel, unsigned tl,
    snd_channel_receipt_t *receipt, uint64_t deadline) {
    assert(channel < (unsigned)streams[0].channels && tl <= UINT8_MAX && deadline > now);
    assert(gain_features & AICA_DRIVER_FEATURE_OBSERVATION_TL);
    if(channel == 1 && gain_submit_error) {
        errno = gain_submit_error;
        gain_submit_error = 0;
        return -1;
    }
    operations[channel] = SND_OBSERVATION_UPDATE;
    gain_tokens[channel] = ++token;
    gain_tl[channel] = tl;
    ++gain_submissions[channel];
    *receipt = (snd_channel_receipt_t){session, token, channel};
    return 0;
}
static int snd_observation_poll_until(const snd_channel_receipt_t *receipt,
    uint64_t deadline, snd_channel_observation_t *obs) {
    assert(receipt->token && receipt->channel < (unsigned)streams[0].channels);
    unsigned ch = receipt->channel;
    bool gain = receipt->token == gain_tokens[ch];
    bool update = operations[ch] == SND_OBSERVATION_UPDATE;
    bool stop = operations[ch] == SND_OBSERVATION_STOP;
    uint64_t challenge_started = now;
    if(stop && stop_errno) {
        errno = stop_errno;
        return -1;
    }
    if(stop && (int)ch == held_stop_channel)
        ++now;
    if(update)
        now += ch == 0 ? left_observation_delay : right_observation_delay;
    if(gain && (int)ch == gain_timeout_channel)
        now = deadline;
    if(receipt->session != session || now >= deadline) {
        errno = receipt->session != session ? ENODEV : ETIMEDOUT;
        return -1;
    }
    *obs = (snd_channel_observation_t){
        .session = session, .token = receipt->token, .channel = ch,
        .operation = operations[ch], .result = SND_OBSERVATION_APPLIED,
        .attribution_kind = SND_OBSERVATION_ATTR_MANAGED,
        .attribution_token = receipt->token - (update && (int)ch == bad_channel),
        .position = position[ch],
        .key_on = (update && (syncs || (gain && (int)ch == delayed_tl_key_on))) ||
                  (operations[ch] == SND_OBSERVATION_START && (int)ch == delayed_start_key_on) ||
                  (stop && (int)ch == held_stop_channel),
        .host_challenge_started_ms = challenge_started, .host_observed_ms = now
    };
    if(gain && (int)ch == gain_reject_channel)
        obs->result = AICA_OBSERVATION_BAD_FLAGS;
    if(gain && (int)ch == gain_stale_channel)
        --obs->token;
    if(gain && (int)ch == gain_unattributed_channel)
        obs->attribution_kind = AICA_OBSERVATION_ATTR_LEGACY;
    return 0;
}
static int snd_observation_consume_until(const snd_channel_receipt_t *receipt,
    uint64_t deadline) {
    assert(receipt->token && receipt->channel < (unsigned)streams[0].channels && deadline > now);
    if(operations[receipt->channel] == SND_OBSERVATION_STOP &&
       (int)receipt->channel == timeout_ack_channel) {
        now = deadline;
        errno = ETIMEDOUT;
        return -1;
    }
    if(!syncs && receipt->token == gain_tokens[receipt->channel])
        key_off_tl_acks |= 1u << receipt->channel;
    return 0;
}
static int snd_channels_start_sync(uint64_t channels) {
    assert(channels == 1 || channels == 3);
    if(gain_submissions[0] || gain_submissions[1])
        assert(key_off_tl_acks == channels);
    tl_at_sync[0] = gain_tl[0];
    tl_at_sync[1] = gain_tl[1];
    ++syncs;
    now += first_delay;
    return 0;
}
