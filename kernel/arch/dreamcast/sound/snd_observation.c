/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <errno.h>
#include <string.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <dc/sound/observation.h>
#include <dc/sound/aica_observation.h>
#include "snd_iface_internal.h"

/* An undefined weak reference must not extract the optional playback object
   from the archive. Its own translation unit retains a strong definition.
   When playback APIs are linked, exclusive lifecycle notifications stay live. */
extern void snd_playback_reset(void) __attribute__((weak));

typedef struct observed_slot {
    uint32_t token, operation;
    bool observed, ack_pending;
} observed_slot_t;

static observed_slot_t slots[64];
static mutex_t bind_mutex = MUTEX_INITIALIZER;
#define OBS_LOCKS8 MUTEX_INITIALIZER, MUTEX_INITIALIZER, MUTEX_INITIALIZER, \
    MUTEX_INITIALIZER, MUTEX_INITIALIZER, MUTEX_INITIALIZER, MUTEX_INITIALIZER, \
    MUTEX_INITIALIZER
static mutex_t channel_mutex[64] = {
    OBS_LOCKS8, OBS_LOCKS8, OBS_LOCKS8, OBS_LOCKS8,
    OBS_LOCKS8, OBS_LOCKS8, OBS_LOCKS8, OBS_LOCKS8
};
#undef OBS_LOCKS8
static uint32_t session_counter, session, cookie;
static bool bound, faulted;

/* Called only by the existing exclusive load/clear lifecycle. */
void snd_observation_load_begin(void) {
    if(snd_playback_reset)
        snd_playback_reset();
    session = session_counter == UINT32_MAX ? 0 : ++session_counter;
    cookie = 0;
    bound = faulted = false;
    memset(slots, 0, sizeof(slots));
}

void snd_observation_reset(void) {
    if(snd_playback_reset)
        snd_playback_reset();
    session = 0;
    bound = false;
    faulted = true;
    memset(slots, 0, sizeof(slots));
}

static int available(void) {
    bool lost;
    int old;
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!snd_iface_is_initialized()) {
        errno = ENODEV;
        return -1;
    }
    if(!(snd_iface_observation_features() & SND_DRIVER_FEATURE_OBSERVATION)) {
        errno = ENOTSUP;
        return -1;
    }
    old = irq_disable();
    lost = faulted;
    irq_restore(old);
    if(!session || lost) {
        errno = lost ? ENODEV : EOVERFLOW;
        return -1;
    }
    return 0;
}

/* Lifecycle is exclusive with callers; no reattachment or generation adoption. */
int snd_observation_session(uint32_t *value) {
    if(available() < 0)
        return -1;
    *value = session;
    return 0;
}

static int next_cookie(uint32_t *value) {
    int old = irq_disable();
    if(cookie == UINT32_MAX) {
        irq_restore(old);
        errno = EOVERFLOW;
        return -1;
    }
    *value = ++cookie;
    irq_restore(old);
    return 0;
}

static int time_left(uint64_t deadline, uint32_t *remaining) {
    uint64_t now = timer_ms_gettime64();
    if(now >= deadline) {
        errno = ETIMEDOUT;
        return -1;
    }
    *remaining = (uint32_t)(deadline - now);
    return 0;
}

static int lock_until(mutex_t *mutex, uint64_t deadline) {
    uint32_t remaining;
    if(time_left(deadline, &remaining) < 0 ||
       mutex_lock_timed(mutex, remaining) < 0)
        return -1;
    if(time_left(deadline, &remaining) < 0) {
        int saved = errno;
        mutex_unlock(mutex);
        errno = saved;
        return -1;
    }
    return 0;
}

static int unlock_result(mutex_t *mutex, int result) {
    int saved = errno;
    mutex_unlock(mutex);
    errno = saved;
    return result;
}

static void packet_init(aica_cmd_t *p, uint32_t words, uint32_t operation,
                         uint32_t identity) {
    p->size = words;
    p->cmd = operation;
    p->cmd_id = identity;
}

static int read_header(aica_observation_header_t *h) {
    if(snd_iface_observation_read(AICA_MEM_OBSERVATION_HEADER, h, sizeof(*h)) < 0)
        return -1;
    if(!h->ready || h->layout_version != AICA_OBSERVATION_LAYOUT_VERSION) {
        errno = ENODEV;
        return -1;
    }
    return 0;
}

static int check_session(void) {
    aica_observation_header_t h;
    if(available() < 0 || read_header(&h) < 0)
        return -1;
    if(h.active_session != session) {
        int old = irq_disable();
        faulted = true;
        irq_restore(old);
        errno = ENODEV;
        return -1;
    }
    return 0;
}

static int ensure_bound(uint64_t deadline) {
    uint32_t words[9] = {0}, identity, remaining;
    aica_cmd_t *packet = (aica_cmd_t *)words;
    aica_observation_header_t h;
    int result = -1;
    if(lock_until(&bind_mutex, deadline) < 0)
        return -1;
    if(available() < 0)
        goto done;
    if(bound) {
        result = check_session();
        goto done;
    }
    if(next_cookie(&identity) < 0)
        goto done;
    packet_init(packet, 9, AICA_CMD_OBSERVATION_BIND, identity);
    words[8] = session;
    if(snd_iface_observation_submit(words, 9, deadline) < 0)
        goto done;
    while(time_left(deadline, &remaining) == 0) {
        if(read_header(&h) < 0) {
            if(errno != EAGAIN)
                goto done;
        }
        else if(h.bind_cookie == identity && h.bind_session == session) {
            if(h.bind_result != AICA_OBSERVATION_BIND_OK ||
               h.active_session != session) {
                int old = irq_disable();
                faulted = true;
                irq_restore(old);
                errno = ENODEV;
                goto done;
            }
            if(time_left(deadline, &remaining) < 0)
                goto done;
            bound = true;
            result = 0;
            goto done;
        }
        thd_pass();
    }
done:
    return unlock_result(&bind_mutex, result);
}

int snd_observation_bind_until(uint64_t deadline, uint32_t *value) {
    if(!value) { errno = EINVAL; return -1; }
    if(ensure_bound(deadline) < 0 || check_session() < 0)
        return -1;
    *value = session;
    return 0;
}

int snd_observation_submit_validated_until(unsigned channel, uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, uint32_t flags,
    snd_channel_receipt_t *receipt, uint64_t deadline) {
    uint32_t words[52] = {0}, identity;
    aica_cmd_t *packet = (aica_cmd_t *)words;
    aica_observation_control_t *control = (void *)packet->cmd_data;
    int result = -1;
    if(channel >= 64 || !receipt) {
        errno = EINVAL;
        return -1;
    }
    if(available() < 0 || ensure_bound(deadline) < 0 ||
       lock_until(&channel_mutex[channel], deadline) < 0)
        return -1;
    if(slots[channel].token) {
        errno = EBUSY;
        goto done;
    }
    if(check_session() < 0 || next_cookie(&identity) < 0)
        goto done;
    packet_init(packet, 52, AICA_CMD_OBSERVATION_CONTROL, identity);
    control->session = session;
    control->token = identity;
    control->result_slot = control->channel = channel;
    control->operation = operation;
    control->fields = fields;
    control->start_flags = flags;
    if(config)
        snd_iface_observation_pack(&control->config, config);
    if(snd_iface_observation_submit(words, 52, deadline) < 0)
        goto done;
    slots[channel].token = identity;
    slots[channel].operation = operation;
    slots[channel].observed = slots[channel].ack_pending = false;
    *receipt = (snd_channel_receipt_t){ session, identity, channel };
    result = 0; /* Do not erase known admission if the clock passed deadline. */
done:
    return unlock_result(&channel_mutex[channel], result);
}

static int receipt_lock(const snd_channel_receipt_t *r, uint64_t deadline) {
    if(!r || r->channel >= 64 || !r->token) {
        errno = EINVAL;
        return -1;
    }
    if(available() < 0 || lock_until(&channel_mutex[r->channel], deadline) < 0)
        return -1;
    if(r->session != session || slots[r->channel].token != r->token) {
        errno = EINVAL;
        return unlock_result(&channel_mutex[r->channel], -1);
    }
    if(check_session() < 0)
        return unlock_result(&channel_mutex[r->channel], -1);
    return 0;
}

int snd_observation_poll_until(const snd_channel_receipt_t *r, uint64_t deadline,
                               snd_channel_observation_t *out) {
    uint32_t words[10] = {0}, identity, remaining;
    uint64_t started, finished;
    aica_cmd_t *packet = (aica_cmd_t *)words;
    aica_observation_result_t result_record;
    aica_observation_channel_t channel_record;
    snd_channel_observation_t value = {0};
    int result = -1;
    if(!out) {
        errno = EINVAL;
        return -1;
    }
    if(receipt_lock(r, deadline) < 0)
        return -1;
    if(slots[r->channel].ack_pending) {
        errno = EBUSY;
        goto done;
    }
    if(next_cookie(&identity) < 0)
        goto done;
    packet_init(packet, 10, AICA_CMD_OBSERVATION_SAMPLE, identity);
    words[8] = r->session;
    words[9] = r->channel;
    started = timer_ms_gettime64();
    if(snd_iface_observation_submit(words, 10, deadline) < 0)
        goto done;
    while(time_left(deadline, &remaining) == 0) {
        if(check_session() < 0) {
            if(errno == EAGAIN) { thd_pass(); continue; }
            goto done;
        }
        if(snd_iface_observation_read(AICA_MEM_OBSERVATION_CHANNELS + r->channel * 32,
                                      &channel_record, sizeof(channel_record)) < 0) {
            if(errno == EAGAIN) { thd_pass(); continue; }
            goto done;
        }
        if(channel_record.session != r->session ||
           channel_record.challenge_cookie != identity) {
            thd_pass();
            continue;
        }
        if(snd_iface_observation_read(AICA_MEM_OBSERVATION_RESULTS + r->channel * 64,
                                      &result_record, sizeof(result_record)) < 0) {
            if(errno == EAGAIN) { thd_pass(); continue; }
            goto done;
        }
        if(result_record.session != r->session || result_record.token != r->token ||
           result_record.channel != r->channel ||
           result_record.operation != slots[r->channel].operation ||
           result_record.result < AICA_OBSERVATION_APPLIED ||
           result_record.result > AICA_OBSERVATION_STALE_TOKEN ||
           channel_record.attribution_kind > AICA_OBSERVATION_ATTR_LEGACY ||
           channel_record.key_on > 1 || channel_record.position > 65535) {
            errno = EPROTO;
            goto done;
        }
        if(check_session() < 0 || time_left(deadline, &remaining) < 0)
            goto done;
        finished = timer_ms_gettime64();
        if(finished < started || finished >= deadline) {
            errno = ETIMEDOUT;
            goto done;
        }
        value.session = r->session;
        value.token = r->token;
        value.channel = r->channel;
        value.operation = result_record.operation;
        value.result = result_record.result;
        value.attribution_token = channel_record.attribution_token;
        value.attribution_kind = channel_record.attribution_kind;
        value.position = channel_record.position;
        value.key_on = channel_record.key_on;
        value.processing_tick = result_record.processing_tick;
        value.observation_tick = channel_record.observation_tick;
        value.host_challenge_started_ms = started;
        value.host_observed_ms = finished;
        slots[r->channel].observed = true;
        *out = value;
        result = 0;
        goto done;
    }
done:
    return unlock_result(&channel_mutex[r->channel], result);
}

int snd_observation_consume_until(const snd_channel_receipt_t *r, uint64_t deadline) {
    uint32_t words[11] = {0}, identity, remaining;
    aica_cmd_t *packet = (aica_cmd_t *)words;
    aica_observation_result_t record;
    int result = -1;
    if(receipt_lock(r, deadline) < 0)
        return -1;
    if(!slots[r->channel].observed) {
        errno = EBUSY;
        goto done;
    }
    if(next_cookie(&identity) < 0)
        goto done;
    packet_init(packet, 11, AICA_CMD_OBSERVATION_CONSUME, identity);
    words[8] = r->session;
    words[9] = r->channel;
    words[10] = r->token;
    if(snd_iface_observation_submit(words, 11, deadline) < 0)
        goto done;
    slots[r->channel].ack_pending = true;
    while(time_left(deadline, &remaining) == 0) {
        if(check_session() < 0) {
            if(errno == EAGAIN) { thd_pass(); continue; }
            goto done;
        }
        if(snd_iface_observation_read(AICA_MEM_OBSERVATION_RESULTS + r->channel * 64,
                                      &record, sizeof(record)) < 0) {
            if(errno == EAGAIN) { thd_pass(); continue; }
            goto done;
        }
        if(record.session == r->session && record.ack_cookie == identity) {
            if(record.ack_token != r->token ||
               record.ack_result != AICA_OBSERVATION_ACK_OK || record.token) {
                errno = EPROTO;
                goto done;
            }
            if(check_session() < 0 || time_left(deadline, &remaining) < 0)
                goto done;
            memset(&slots[r->channel], 0, sizeof(slots[r->channel]));
            result = 0;
            goto done;
        }
        thd_pass();
    }
done:
    return unlock_result(&channel_mutex[r->channel], result);
}

int snd_observation_submit_until(unsigned channel, uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, uint32_t flags,
    snd_channel_receipt_t *receipt, uint64_t deadline) {
    if(channel >= 64 || !receipt) { errno = EINVAL; return -1; }
    if(available() < 0 ||
       snd_iface_observation_validate(operation, config, fields, flags) < 0)
        return -1;
    return snd_observation_submit_validated_until(channel, operation, config,
                                                fields, flags, receipt, deadline);
}

int snd_channel_observed_submit(unsigned channel, uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, uint32_t flags,
    snd_channel_receipt_t *receipt) {
    return snd_observation_submit_until(channel, operation, config, fields, flags,
        receipt, timer_ms_gettime64() + SND_OBSERVATION_SUBMIT_TIMEOUT_MS);
}

int snd_channel_observed_poll(const snd_channel_receipt_t *receipt, uint32_t timeout,
                              snd_channel_observation_t *observation) {
    if(!timeout) { errno = EINVAL; return -1; }
    return snd_observation_poll_until(receipt, timer_ms_gettime64() + timeout,
                                     observation);
}

int snd_channel_observed_consume(const snd_channel_receipt_t *receipt,
                                 uint32_t timeout) {
    if(!timeout) { errno = EINVAL; return -1; }
    return snd_observation_consume_until(receipt, timer_ms_gettime64() + timeout);
}

/* Current-generation optional record. No receipt adoption or result consumption. */
int snd_observation_completion_until(unsigned channel, uint32_t expected_session,
    uint64_t deadline, aica_completion_channel_t *out,
    uint64_t *start_out, uint64_t *finish_out) {
    uint32_t words[10] = {0}, identity, remaining;
    aica_cmd_t *packet = (aica_cmd_t *)words;
    aica_completion_header_t h;
    aica_completion_channel_t c;
    uint64_t started, finished;
    int result = -1;
    if(channel >= 64 || !out || !start_out || !finish_out) {
        errno = EINVAL; return -1;
    }
    if(available() < 0) return -1;
    if(!(snd_iface_observation_features() & AICA_DRIVER_FEATURE_COMPLETION)) {
        errno = ENOTSUP; return -1;
    }
    if(expected_session != session) { errno = ENODEV; return -1; }
    if(ensure_bound(deadline) < 0 || lock_until(&channel_mutex[channel], deadline) < 0)
        return -1;
    if(slots[channel].token) { errno = EBUSY; goto done; }
    if(check_session() < 0 || next_cookie(&identity) < 0) goto done;
    packet_init(packet, 10, AICA_CMD_OBSERVATION_SAMPLE, identity);
    words[8] = session; words[9] = channel;
    started = timer_ms_gettime64();
    if(snd_iface_observation_submit(words, 10, deadline) < 0) goto done;
    while(time_left(deadline, &remaining) == 0) {
        if(check_session() < 0) {
            if(errno == EAGAIN) { thd_pass(); continue; }
            goto done;
        }
        if(snd_iface_observation_read(AICA_MEM_COMPLETION_HEADER, &h, sizeof(h)) < 0 ||
           snd_iface_observation_read(AICA_MEM_COMPLETION_CHANNELS + channel * 64,
                                      &c, sizeof(c)) < 0) {
            if(errno == EAGAIN) { thd_pass(); continue; }
            goto done;
        }
        if(h.layout_version != AICA_COMPLETION_LAYOUT_VERSION || h.ready != 1) {
            errno = EPROTO; goto done;
        }
        if(h.active_session != expected_session || c.session != expected_session) {
            errno = ENODEV; goto done;
        }
        if(c.challenge_cookie != identity) { thd_pass(); continue; }
        for(unsigned i = 0; i < 12; ++i)
            if(h.reserved[i]) { errno = EPROTO; goto done; }
        if(c.state > AICA_COMPLETION_INTERFERED ||
           c.attribution_kind > AICA_OBSERVATION_ATTR_LEGACY ||
           (c.flags & ~(AICA_COMPLETION_ELIGIBLE | AICA_COMPLETION_END_SEEN)) ||
           c.key_on > 1 || c.position > 65535 || c.loop_enabled > 1 ||
           c.format > AICA_SM_ADPCM_LS || c.reserved[0] || c.reserved[1] ||
           (c.flags && !c.start_token) ||
           ((c.flags & AICA_COMPLETION_ELIGIBLE) &&
            (c.state != AICA_COMPLETION_ACTIVE || c.loop_enabled || c.format != AICA_SM_16BIT)) ||
           ((c.flags & AICA_COMPLETION_END_SEEN) &&
            (c.loop_enabled || c.format != AICA_SM_16BIT ||
             (c.state == AICA_COMPLETION_ACTIVE && !(c.flags & AICA_COMPLETION_ELIGIBLE)))) ||
           (!(c.flags & AICA_COMPLETION_END_SEEN) && c.first_end_tick) ||
           (c.state == AICA_COMPLETION_EMPTY && (c.start_token || c.command_token ||
             c.attribution_kind != AICA_OBSERVATION_ATTR_NONE)) ||
           (c.state == AICA_COMPLETION_ACTIVE && !c.start_token) ||
           (c.attribution_kind == AICA_OBSERVATION_ATTR_MANAGED && !c.command_token) ||
           (c.attribution_kind == AICA_OBSERVATION_ATTR_LEGACY &&
            (c.command_token || c.state != AICA_COMPLETION_INTERFERED)) ||
           (c.attribution_kind == AICA_OBSERVATION_ATTR_NONE &&
            c.state != AICA_COMPLETION_EMPTY)) {
            errno = EPROTO; goto done;
        }
        if(check_session() < 0 || time_left(deadline, &remaining) < 0) goto done;
        finished = timer_ms_gettime64();
        if(finished < started || finished >= deadline) { errno = ETIMEDOUT; goto done; }
        *out = c; *start_out = started; *finish_out = finished;
        result = 0; goto done;
    }
done:
    return unlock_result(&channel_mutex[channel], result);
}
