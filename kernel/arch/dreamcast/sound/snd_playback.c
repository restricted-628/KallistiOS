/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Cooperative command policy only. No sample allocator or retirement endpoint. */
#include <errno.h>
#include <string.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/timer.h>
#include <dc/sound/playback.h>
#include "snd_port_internal.h"
#include "snd_iface_internal.h"

_Static_assert(SND_DRIVER_FEATURE_COMPLETION == AICA_DRIVER_FEATURE_COMPLETION &&
    SND_PLAYBACK_EMPTY == AICA_COMPLETION_EMPTY &&
    SND_PLAYBACK_ACTIVE == AICA_COMPLETION_ACTIVE &&
    SND_PLAYBACK_STOPPED == AICA_COMPLETION_STOPPED &&
    SND_PLAYBACK_INTERFERED == AICA_COMPLETION_INTERFERED &&
    SND_PLAYBACK_END_ELIGIBLE == AICA_COMPLETION_ELIGIBLE &&
    SND_PLAYBACK_END_SEEN == AICA_COMPLETION_END_SEEN, "completion public mapping");

enum pb_control { PB_EMPTY, PB_ACTIVE, PB_STOPPED, PB_INTERFERED, PB_LOST };
typedef struct playback_state {
    snd_port_t port;
    uint32_t session, channel, operation, fields, last_applied;
    uint32_t start_token, end_reset_token;
    enum pb_control control;
    bool configured, observed, transaction_live;
    snd_port_transaction_t transaction;
    snd_channel_receipt_t receipt;
    snd_channel_config_t committed, proposed;
} playback_state_t;
static playback_state_t pb_states[SND_PORT_SLOT_COUNT];
#define PB_LOCKS8 MUTEX_INITIALIZER, MUTEX_INITIALIZER, MUTEX_INITIALIZER, \
    MUTEX_INITIALIZER, MUTEX_INITIALIZER, MUTEX_INITIALIZER, MUTEX_INITIALIZER, \
    MUTEX_INITIALIZER
static mutex_t pb_mutex[SND_PORT_SLOT_COUNT] = {
    PB_LOCKS8, PB_LOCKS8, PB_LOCKS8, PB_LOCKS8,
    PB_LOCKS8, PB_LOCKS8, PB_LOCKS8, PB_LOCKS8
};
#undef PB_LOCKS8

/* Existing sound lifecycle is exclusive with API callers. Preserve every live
   obligation and pin; a reload never adopts an old attachment automatically. */
void snd_playback_reset(void) {
    for(unsigned i = 0; i < SND_PORT_SLOT_COUNT; ++i)
        if(pb_states[i].port)
            pb_states[i].control = PB_LOST;
}

static int pb_lock(snd_port_t port, uint64_t deadline) {
    uint64_t now;
    mutex_t *mutex = &pb_mutex[SND_PORT_SLOT_INDEX(port)];
    if(irq_inside_int()) { errno = EPERM; return -1; }
    now = timer_ms_gettime64();
    if(now >= deadline) { errno = ETIMEDOUT; return -1; }
    if(mutex_lock_timed(mutex, (uint32_t)(deadline - now)) < 0)
        return -1;
    if(timer_ms_gettime64() >= deadline) {
        mutex_unlock(mutex);
        errno = ETIMEDOUT;
        return -1;
    }
    return 0;
}

static int pb_unlock(snd_port_t port, int result) {
    int saved = errno;
    mutex_unlock(&pb_mutex[SND_PORT_SLOT_INDEX(port)]);
    errno = saved;
    return result;
}

/* Must run after acquiring the per-slot mutex: close/reuse can happen while
   the caller waits. Port hooks also revalidate the opaque generation. */
static playback_state_t *pb_lookup(snd_port_t port) {
    playback_state_t *s = &pb_states[SND_PORT_SLOT_INDEX(port)];
    uint32_t generation;
    if(!port || s->port != port || snd_port_playback_validate(port) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if(s->control == PB_LOST) { errno = ENODEV; return NULL; }
    if(snd_observation_session(&generation) < 0) {
        if(errno == ENODEV)
            s->control = PB_LOST;
        return NULL;
    }
    if(generation != s->session) {
        s->control = PB_LOST;
        errno = ENODEV;
        return NULL;
    }
    return s;
}

int snd_port_playback_attach(snd_port_t port) {
    uint64_t deadline = timer_ms_gettime64() + SND_PLAYBACK_TIMEOUT_MS;
    uint32_t generation;
    unsigned channel;
    playback_state_t *s = &pb_states[SND_PORT_SLOT_INDEX(port)];
    if(pb_lock(port, deadline) < 0)
        return -1;
    if(snd_observation_session(&generation) < 0 ||
       snd_port_playback_pin(port, &channel) < 0)
        return pb_unlock(port, -1);
    memset(s, 0, sizeof(*s));
    s->port = port;
    s->channel = channel;
    s->session = generation;
    return pb_unlock(port, 0);
}

static int pb_current(playback_state_t *s, uint64_t deadline,
                       snd_port_playback_state_t *out);

static int pb_submit(snd_port_t port, uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, unsigned priority,
    snd_port_operation_t *out) {
    uint64_t deadline = timer_ms_gettime64() + SND_PLAYBACK_TIMEOUT_MS;
    playback_state_t *s;
    snd_port_transaction_t transaction;
    snd_channel_receipt_t receipt;
    bool transaction_live = false;
    int result;
    if(!out || priority > 15 ||
       snd_iface_observation_validate(operation, config, fields, 0) < 0) {
        errno = EINVAL;
        return -1;
    }
    if(pb_lock(port, deadline) < 0)
        return -1;
    s = pb_lookup(port);
    if(!s)
        return pb_unlock(port, -1);
    if(s->receipt.token ||
       (s->control == PB_INTERFERED && operation != SND_OBSERVATION_STOP)) {
        errno = EBUSY;
        return pb_unlock(port, -1);
    }
    if(operation == SND_OBSERVATION_UPDATE && !s->configured) {
        errno = EINVAL;
        return pb_unlock(port, -1);
    }
    if(operation == SND_OBSERVATION_START) {
        if(s->start_token && s->control == PB_ACTIVE &&
           (snd_iface_observation_features() & SND_DRIVER_FEATURE_COMPLETION)) {
            snd_port_playback_state_t current;
            if(pb_current(s, deadline, &current) < 0)
                return pb_unlock(port, -1);
            if(s->control == PB_INTERFERED) {
                errno = EBUSY; return pb_unlock(port, -1);
            }
        }
        result = snd_port_begin(port, priority, &transaction);
        if(result <= 0)
            return pb_unlock(port, result);
        transaction_live = true;
    }
    result = snd_observation_submit_validated_until(s->channel, operation, config, fields,
                                         0, &receipt, deadline);
    if(result < 0) {
        int saved = errno;
        if(transaction_live)
            snd_port_finish(transaction, false); /* Known CONTROL refusal only. */
        if(saved == ENODEV)
            s->control = PB_LOST;
        errno = saved;
        return pb_unlock(port, -1);
    }
    s->receipt = receipt;
    s->operation = operation;
    s->fields = fields;
    s->observed = false;
    s->transaction_live = transaction_live;
    if(transaction_live)
        s->transaction = transaction;
    if(config)
        s->proposed = *config;
    *out = (snd_port_operation_t){ port, receipt.session, receipt.token };
    return pb_unlock(port, operation == SND_OBSERVATION_START ? 1 : 0);
}

int snd_port_playback_start(snd_port_t port, const snd_channel_config_t *config,
    unsigned priority, snd_port_operation_t *operation) {
    return pb_submit(port, SND_OBSERVATION_START, config, 0, priority, operation);
}

int snd_port_playback_update(snd_port_t port, const snd_channel_config_t *config,
    uint32_t fields, snd_port_operation_t *operation) {
    return pb_submit(port, SND_OBSERVATION_UPDATE, config, fields, 0, operation);
}

int snd_port_playback_stop(snd_port_t port, snd_port_operation_t *operation) {
    return pb_submit(port, SND_OBSERVATION_STOP, NULL, 0, 0, operation);
}

static playback_state_t *pb_operation(const snd_port_operation_t *operation) {
    playback_state_t *s = pb_lookup(operation->port);
    if(s && (operation->session != s->session || !operation->token ||
             operation->token != s->receipt.token)) {
        errno = EINVAL;
        return NULL;
    }
    return s;
}

static void pb_update_config(playback_state_t *s) {
    uint32_t f = s->fields;
    if(f & SND_CHANNEL_UPDATE_FREQUENCY) s->committed.sample_rate = s->proposed.sample_rate;
    if(f & SND_CHANNEL_UPDATE_VOLUME) s->committed.volume = s->proposed.volume;
    if(f & SND_CHANNEL_UPDATE_PAN) s->committed.pan = s->proposed.pan;
    if(f & SND_CHANNEL_UPDATE_ENVELOPE) s->committed.envelope = s->proposed.envelope;
    if(f & SND_CHANNEL_UPDATE_LFO) s->committed.lfo = s->proposed.lfo;
    if(f & SND_CHANNEL_UPDATE_ROUTING) s->committed.routing = s->proposed.routing;
    if(f & SND_CHANNEL_UPDATE_FILTER) s->committed.filter = s->proposed.filter;
}

static int pb_classify(playback_state_t *s, const snd_channel_observation_t *o) {
    bool applied = o->result == SND_OBSERVATION_APPLIED;
    if(!s->observed) {
        if(s->transaction_live) {
            if(snd_port_finish(s->transaction, applied) < 0)
                goto lost;
            s->transaction_live = false;
        }
        if(applied) {
            s->last_applied = o->token;
            if(s->operation == SND_OBSERVATION_START) {
                s->committed = s->proposed;
                s->start_token = o->token;
                s->end_reset_token = 0;
                s->configured = true;
                s->control = PB_ACTIVE;
            }
            else if(s->operation == SND_OBSERVATION_UPDATE)
                pb_update_config(s);
            else {
                if(snd_port_playback_stopped(s->port) < 0)
                    goto lost;
                s->control = PB_STOPPED;
            }
        }
        s->observed = true;
    }
    /* A rejection retains the prior applied token. Historical APPLIED with
       later legacy mutation commits history, then loses current attribution. */
    if(s->last_applied ?
       (o->attribution_kind != SND_OBSERVATION_ATTR_MANAGED ||
        o->attribution_token != s->last_applied) :
       (o->attribution_kind != SND_OBSERVATION_ATTR_NONE || o->attribution_token))
        s->control = PB_INTERFERED;
    return 0;
lost:
    s->control = PB_LOST;
    errno = EPROTO;
    return -1;
}

int snd_port_playback_poll(const snd_port_operation_t *operation,
    uint32_t timeout, snd_channel_observation_t *out) {
    uint64_t deadline = timer_ms_gettime64() + timeout;
    playback_state_t *s;
    snd_channel_observation_t value;
    int result;
    if(!operation || !out || !timeout) { errno = EINVAL; return -1; }
    if(pb_lock(operation->port, deadline) < 0)
        return -1;
    s = pb_operation(operation);
    if(!s)
        return pb_unlock(operation->port, -1);
    result = snd_observation_poll_until(&s->receipt, deadline, &value);
    if(result == 0) {
        result = pb_classify(s, &value);
        if(result == 0)
            *out = value;
    }
    else if(errno == ENODEV)
        s->control = PB_LOST;
    return pb_unlock(operation->port, result);
}

int snd_port_playback_consume(const snd_port_operation_t *operation,
    uint32_t timeout) {
    uint64_t deadline = timer_ms_gettime64() + timeout;
    playback_state_t *s;
    int result;
    if(!operation || !timeout) { errno = EINVAL; return -1; }
    if(pb_lock(operation->port, deadline) < 0)
        return -1;
    s = pb_operation(operation);
    if(!s)
        return pb_unlock(operation->port, -1);
    if(!s->observed) { errno = EBUSY; return pb_unlock(operation->port, -1); }
    result = snd_observation_consume_until(&s->receipt, deadline);
    if(result == 0) {
        memset(&s->receipt, 0, sizeof(s->receipt));
        s->observed = false;
    }
    else if(errno == ENODEV)
        s->control = PB_LOST;
    return pb_unlock(operation->port, result);
}

int snd_port_playback_detach(snd_port_t port) {
    uint64_t deadline = timer_ms_gettime64() + SND_PLAYBACK_TIMEOUT_MS;
    playback_state_t *s;
    int result;
    if(pb_lock(port, deadline) < 0)
        return -1;
    s = pb_lookup(port);
    if(!s)
        return pb_unlock(port, -1);
    if(s->receipt.token || (s->control != PB_EMPTY && s->control != PB_STOPPED)) {
        errno = EBUSY;
        return pb_unlock(port, -1);
    }
    result = snd_port_playback_unpin(port);
    if(result == 0)
        memset(s, 0, sizeof(*s));
    return pb_unlock(port, result);
}

static int pb_current(playback_state_t *s, uint64_t deadline,
                       snd_port_playback_state_t *out) {
    aica_completion_channel_t c;
    snd_port_playback_state_t value = {0};
    uint64_t started, finished;
    if(!(snd_iface_observation_features() & SND_DRIVER_FEATURE_COMPLETION)) {
        errno = ENOTSUP; return -1;
    }
    if(s->receipt.token) { errno = EBUSY; return -1; }
    if(!s->start_token) { errno = EAGAIN; return -1; }
    if(snd_observation_completion_until(s->channel, s->session, deadline,
                                        &c, &started, &finished) < 0) {
        if(errno == ENODEV) s->control = PB_LOST;
        return -1;
    }
    if(c.start_token != s->start_token) {
        s->control = PB_INTERFERED; errno = EBUSY; return -1;
    }
    if(c.attribution_kind != AICA_OBSERVATION_ATTR_MANAGED ||
       c.command_token != s->last_applied || c.state == AICA_COMPLETION_INTERFERED) {
        s->control = PB_INTERFERED;
        if(c.attribution_kind != AICA_OBSERVATION_ATTR_LEGACY ||
           c.state != AICA_COMPLETION_INTERFERED) {
            errno = EBUSY; return -1;
        }
    }
    if(s->control == PB_ACTIVE && c.state == AICA_COMPLETION_ACTIVE &&
       (c.flags & AICA_COMPLETION_END_SEEN) && s->end_reset_token != s->start_token) {
        if(snd_port_playback_stopped(s->port) < 0) {
            s->control = PB_LOST; errno = EPROTO; return -1;
        }
        s->end_reset_token = s->start_token;
    }
    value.port = s->port; value.session = s->session; value.channel = s->channel;
    value.start_token = c.start_token; value.command_token = c.command_token;
    value.state = s->control == PB_INTERFERED ? SND_PLAYBACK_INTERFERED : c.state;
    value.flags = c.flags; value.first_end_tick = c.first_end_tick;
    value.position = c.position; value.key_on = c.key_on;
    value.observation_tick = c.observation_tick;
    value.host_challenge_started_ms = started; value.host_observed_ms = finished;
    *out = value;
    return 0;
}

int snd_port_playback_get_state(snd_port_t port, uint32_t timeout,
                                snd_port_playback_state_t *out) {
    uint64_t deadline = timer_ms_gettime64() + timeout;
    playback_state_t *s;
    int result;
    if(!out || !timeout) { errno = EINVAL; return -1; }
    if(pb_lock(port, deadline) < 0) return -1;
    s = pb_lookup(port);
    if(!s) return pb_unlock(port, -1);
    result = pb_current(s, deadline, out);
    return pb_unlock(port, result);
}
