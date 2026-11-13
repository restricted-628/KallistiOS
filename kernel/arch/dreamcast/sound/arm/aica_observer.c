/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Original command-result retention and observation; no retirement policy. */
#include "aica_observer.h"

static void ordered(void) {
    __asm__ __volatile__("" ::: "memory");
}

static int begin(volatile uint32_t *sequence) {
    uint32_t value = *sequence;
    if(value >= UINT32_MAX - 1 || (value & 1)) {
        *sequence = UINT32_MAX;
        return 0;
    }
    *sequence = value + 1;
    ordered();
    return 1;
}

static void end(volatile uint32_t *sequence) {
    ordered();
    ++*sequence;
}

static void clear_words(volatile uint32_t *words, unsigned count) {
    while(count--)
        *words++ = 0;
}

int aica_observer_needs_lp(const aica_observer_t *o, uint32_t channel) {
    const volatile aica_completion_channel_t *c;
    if(!o->completions || channel >= 64)
        return 0;
    c = &o->completions[channel];
    return c->sequence != UINT32_MAX && c->state == AICA_COMPLETION_ACTIVE &&
           (c->flags & (AICA_COMPLETION_ELIGIBLE | AICA_COMPLETION_END_SEEN)) ==
             AICA_COMPLETION_ELIGIBLE;
}

static void completion_sample(aica_observer_t *o, uint32_t channel,
    uint32_t position, uint32_t key, uint32_t tick, uint32_t lp,
    uint32_t cookie, int set_cookie) {
    volatile aica_completion_channel_t *c;
    if(!o->completions) return;
    c = &o->completions[channel];
    if(begin(&c->sequence)) {
        c->session = o->header->active_session;
        c->position = position; c->key_on = key; c->observation_tick = tick;
        if(set_cookie) c->challenge_cookie = cookie;
        if(lp && c->state == AICA_COMPLETION_ACTIVE &&
           (c->flags & (AICA_COMPLETION_ELIGIBLE | AICA_COMPLETION_END_SEEN)) ==
             AICA_COMPLETION_ELIGIBLE) {
            c->flags |= AICA_COMPLETION_END_SEEN;
            c->first_end_tick = tick;
        }
        end(&c->sequence);
    }
}

/* Install identity before the first post-apply destructive sample. ACK and
   rejected/duplicate commands never reach this transition. */
static void completion_control(aica_observer_t *o,
                                const aica_observation_control_t *p) {
    volatile aica_completion_channel_t *c;
    if(!o->completions) return;
    c = &o->completions[p->channel];
    if(!begin(&c->sequence)) return;
    c->session = p->session; c->command_token = p->token;
    c->attribution_kind = AICA_OBSERVATION_ATTR_MANAGED;
    if(p->operation == AICA_CHANNEL_OP_START) {
        c->start_token = p->token;
        c->state = AICA_COMPLETION_ACTIVE;
        c->flags = !p->start_flags && !p->config.loop &&
                   p->config.type == AICA_SM_16BIT ? AICA_COMPLETION_ELIGIBLE : 0;
        c->first_end_tick = c->challenge_cookie = 0;
        c->format = p->config.type; c->loop_enabled = p->config.loop;
    }
    else if(p->operation == AICA_CHANNEL_OP_STOP) {
        c->state = AICA_COMPLETION_STOPPED;
        c->flags &= ~AICA_COMPLETION_ELIGIBLE;
    }
    /* UPDATE preserves START identity and end history, including interference. */
    end(&c->sequence);
}

static int reject(aica_observer_t *o, uint32_t reason, uint32_t tick) {
    if(begin(&o->header->sequence)) {
        o->header->last_protocol_error = reason;
        ++o->header->protocol_error_count;
        o->header->observation_tick = tick;
        end(&o->header->sequence);
    }
    return 0;
}

void aica_observer_init(aica_observer_t *o) {
    o->header->ready = 0;
    ordered();
    clear_words((volatile uint32_t *)o->header, sizeof(*o->header) / 4);
    clear_words((volatile uint32_t *)o->results, 64 * sizeof(*o->results) / 4);
    clear_words((volatile uint32_t *)o->channels, 64 * sizeof(*o->channels) / 4);
    o->header->layout_version = AICA_OBSERVATION_LAYOUT_VERSION;
    if(o->completion_header) {
        clear_words((volatile uint32_t *)o->completion_header,
                    sizeof(*o->completion_header) / 4);
        clear_words((volatile uint32_t *)o->completions,
                    64 * sizeof(*o->completions) / 4);
        o->completion_header->layout_version = AICA_COMPLETION_LAYOUT_VERSION;
    }
}

void aica_observer_ready(aica_observer_t *o) {
    if(o->completion_header && begin(&o->completion_header->sequence)) {
        o->completion_header->ready = 1;
        end(&o->completion_header->sequence);
    }
    if(begin(&o->header->sequence)) {
        o->header->ready = 1;
        end(&o->header->sequence);
    }
}

void aica_observer_refresh(aica_observer_t *o, uint32_t channel,
                          uint32_t position, uint32_t key_on, uint32_t tick, uint32_t lp) {
    volatile aica_observation_channel_t *c;
    if(channel >= 64)
        return;
    completion_sample(o, channel, position, key_on, tick, lp, 0, 0);
    c = &o->channels[channel];
    if(begin(&c->sequence)) {
        c->session = o->header->active_session;
        c->position = position;
        c->key_on = key_on;
        c->observation_tick = tick;
        end(&c->sequence);
    }
}

static void sample(aica_observer_t *o, uint32_t channel, uint32_t tick,
                    uint32_t cookie, int set_cookie, uint32_t kind,
                    uint32_t token, int set_attribution) {
    uint32_t position, key_on;
    volatile aica_observation_channel_t *c = &o->channels[channel];
    o->read(channel, &position, &key_on);
    /* read() selected the channel. Consume LP exactly once, before publication. */
    uint32_t lp = aica_observer_needs_lp(o, channel) ? o->loop_end() : 0;
    completion_sample(o, channel, position, key_on, tick, lp, cookie, set_cookie);
    if(begin(&c->sequence)) {
        c->session = o->header->active_session;
        c->position = position;
        c->key_on = key_on;
        c->observation_tick = tick;
        if(set_cookie)
            c->challenge_cookie = cookie;
        if(set_attribution) {
            c->attribution_kind = kind;
            c->attribution_token = token;
        }
        end(&c->sequence);
    }
}

void aica_observer_legacy(aica_observer_t *o, uint32_t channel) {
    volatile aica_observation_channel_t *c;
    if(channel >= 64)
        return;
    if(o->completions) {
        volatile aica_completion_channel_t *e = &o->completions[channel];
        if(begin(&e->sequence)) {
            e->session = o->header->active_session;
            e->command_token = 0;
            e->attribution_kind = AICA_OBSERVATION_ATTR_LEGACY;
            e->state = AICA_COMPLETION_INTERFERED;
            e->flags &= ~AICA_COMPLETION_ELIGIBLE;
            end(&e->sequence);
        }
    }
    c = &o->channels[channel];
    if(begin(&c->sequence)) {
        c->session = o->header->active_session;
        c->attribution_kind = AICA_OBSERVATION_ATTR_LEGACY;
        c->attribution_token = 0;
        /* Preserve the sampled tuple, including its challenge and sample tick.
           Attribution changes are not new measurements. SAMPLE and periodic
           refresh advance the tuple; no extra MMIO is needed to invalidate. */
        end(&c->sequence);
    }
}

static int control(aica_observer_t *o, const aica_observation_control_t *p,
                    uint32_t tick) {
    volatile aica_observation_result_t *r;
    aica_channel_control_t c;
    uint32_t outcome;
    if(p->result_slot >= 64)
        return reject(o, AICA_OBSERVATION_DIAG_SLOT, tick);
    r = &o->results[p->result_slot];
    if(r->token) {
        if(r->session == p->session && r->token == p->token)
            return 1; /* Immutable retained result: never replay the operation. */
        return reject(o, AICA_OBSERVATION_DIAG_BUSY, tick);
    }
    if(!begin(&r->sequence))
        return reject(o, AICA_OBSERVATION_DIAG_EXHAUSTED, tick);
    if(p->token <= r->high_water_token)
        outcome = AICA_OBSERVATION_STALE_TOKEN;
    else {
        r->high_water_token = p->token;
        if(p->channel >= 64)
            outcome = AICA_OBSERVATION_BAD_CONFIG;
        else {
            c.operation = p->operation;
            c.fields = p->fields;
            c.start_flags = p->start_flags;
            c.config = p->config;
            outcome = o->apply(p->channel, &c);
            if(outcome == AICA_OBSERVATION_APPLIED) {
                completion_control(o, p);
                sample(o, p->channel, tick, 0, 0,
                       AICA_OBSERVATION_ATTR_MANAGED, p->token, 1);
            }
        }
    }
    r->session = p->session;
    r->token = p->token;
    r->channel = p->channel;
    r->operation = p->operation;
    r->result = outcome;
    r->processing_tick = tick;
    r->ack_cookie = r->ack_token = r->ack_result = 0;
    end(&r->sequence);
    return outcome == AICA_OBSERVATION_APPLIED;
}

int aica_observer_command(aica_observer_t *o, const aica_cmd_t *packet,
                          uint32_t tick) {
    const uint32_t *payload = (const uint32_t *)packet->cmd_data;
    uint32_t expected, session;
    unsigned i;
    switch(packet->cmd) {
        case AICA_CMD_OBSERVATION_BIND: expected = 9; break;
        case AICA_CMD_OBSERVATION_CONTROL: expected = 52; break;
        case AICA_CMD_OBSERVATION_SAMPLE: expected = 10; break;
        case AICA_CMD_OBSERVATION_CONSUME: expected = 11; break;
        default: return reject(o, AICA_OBSERVATION_DIAG_PACKET, tick);
    }
    if(packet->size != expected || packet->timestamp || !packet->cmd_id)
        return reject(o, AICA_OBSERVATION_DIAG_PACKET, tick);
    for(i = 0; i < 4; ++i)
        if(packet->misc[i])
            return reject(o, AICA_OBSERVATION_DIAG_PACKET, tick);
    session = payload[0]; /* Exact size is now known. */
    if(!session || !o->header->ready || o->header->sequence == UINT32_MAX)
        return reject(o, AICA_OBSERVATION_DIAG_SESSION, tick);
    if(packet->cmd == AICA_CMD_OBSERVATION_BIND) {
        uint32_t result = !o->header->active_session ||
            o->header->active_session == session ?
            AICA_OBSERVATION_BIND_OK : AICA_OBSERVATION_BIND_CONFLICT;
        if(!begin(&o->header->sequence))
            return 0;
        if(result == AICA_OBSERVATION_BIND_OK) {
            o->header->active_session = session;
            if(o->completion_header && begin(&o->completion_header->sequence)) {
                o->completion_header->active_session = session;
                end(&o->completion_header->sequence);
            }
        }
        o->header->bind_session = session;
        o->header->bind_cookie = packet->cmd_id;
        o->header->bind_result = result;
        o->header->observation_tick = tick;
        end(&o->header->sequence);
        return result == AICA_OBSERVATION_BIND_OK;
    }
    if(session != o->header->active_session)
        return reject(o, AICA_OBSERVATION_DIAG_SESSION, tick);
    if(packet->cmd == AICA_CMD_OBSERVATION_CONTROL) {
        const aica_observation_control_t *p = (const void *)payload;
        if(!p->token || p->token != packet->cmd_id)
            return reject(o, AICA_OBSERVATION_DIAG_PACKET, tick);
        return control(o, p, tick);
    }
    if(packet->cmd == AICA_CMD_OBSERVATION_SAMPLE) {
        if(payload[1] >= 64)
            return reject(o, AICA_OBSERVATION_DIAG_CHANNEL, tick);
        sample(o, payload[1], tick, packet->cmd_id, 1, 0, 0, 0);
        return 1;
    }
    else {
        volatile aica_observation_result_t *r;
        uint32_t token = payload[2], outcome;
        if(payload[1] >= 64 || !token)
            return reject(o, AICA_OBSERVATION_DIAG_SLOT, tick);
        r = &o->results[payload[1]];
        if(!begin(&r->sequence))
            return reject(o, AICA_OBSERVATION_DIAG_EXHAUSTED, tick);
        outcome = (r->session == session &&
                   (r->token == token || (!r->token && r->ack_token == token &&
                     r->ack_result == AICA_OBSERVATION_ACK_OK))) ?
                  AICA_OBSERVATION_ACK_OK : AICA_OBSERVATION_ACK_MISMATCH;
        if(outcome == AICA_OBSERVATION_ACK_OK) {
            r->token = 0;
            r->result = AICA_OBSERVATION_EMPTY;
        }
        r->ack_cookie = packet->cmd_id;
        r->ack_token = token;
        r->ack_result = outcome;
        end(&r->sequence);
        return outcome == AICA_OBSERVATION_ACK_OK;
    }
}
