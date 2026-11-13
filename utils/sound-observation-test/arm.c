/* Execute the production copied-packet dispatcher and observer with fake MMIO. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "aica_cmd_iface.h"
static uint32_t test_clock;
#undef AICA_MEM_CLOCK
#define AICA_MEM_CLOCK ((uintptr_t)&test_clock)
#undef memcpy
#define memcpy firmware_memcpy
#include "../../../kernel/arch/dreamcast/sound/arm/main.c"
#undef memcpy
static aica_observation_header_t header;
static aica_completion_header_t completion_header;
static aica_completion_channel_t completions[64];
static unsigned lp_reads, selected_channel;
static uint32_t lp_bits[64], lp_on_start;
uint32 aica_get_loop_end(void) {
    ++lp_reads; uint32_t v = lp_bits[selected_channel]; lp_bits[selected_channel] = 0; return v;
}
static aica_observation_result_t results[64];
static aica_observation_channel_t observations[64];
static aica_channel_t legacy[64];
static aica_channel_status_ext_t statuses[64];
static unsigned writes, reads, checks, position_reads, key_reads;
static unsigned full_validations, field_validations;
/* Compiler entry hooks count the actual production validators, including
   inlined entries, without adding test instrumentation to production source. */
void __cyg_profile_func_enter(void *, void *) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *, void *) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn, void *caller) {
    (void)caller;
    if(fn == (void *)channel_config_valid) ++full_validations;
    if(fn == (void *)config_fields_valid) ++field_validations;
}
void __cyg_profile_func_exit(void *fn, void *caller) { (void)fn; (void)caller; }
static uint32_t selected_low, selected_high;
#define CHECK(x) do { ++checks; assert(x); } while(0)
static void touch(int ch) { assert(ch >= 0 && ch < 64); ++writes; }
void aica_init(void) {}
void aica_play(int ch, int delay) { (void)delay; touch(ch); }
void aica_sync_play(uint32 low, uint32 high) {
    selected_low = low; selected_high = high; ++writes;
}
void aica_stop(int ch) { touch(ch); }
void aica_vol(int ch) { touch(ch); }
void aica_pan(int ch) { touch(ch); }
void aica_freq(int ch) { touch(ch); }
int aica_get_pos(int ch) { assert(ch >= 0 && ch < 64); ++reads; ++position_reads; selected_channel = ch; chans[ch].pos = 123; return 123; }
int aica_is_playing(int ch) { assert(ch >= 0 && ch < 64); ++reads; ++key_reads; return 0; }
void aica_channel_start(int ch, const aica_channel_config_t *c, int d) {
    (void)c; (void)d; touch(ch); lp_bits[ch] = lp_on_start;
}
void aica_channel_update(int ch, const aica_channel_config_t *c, uint32 f) {
    (void)c; (void)f; touch(ch);
}
static uint32_t data[64];
static aica_cmd_t *packet(void) { return (void *)data; }
static void init_packet(uint32_t cmd, uint32_t size, uint32_t id) {
    memset(data, 0, sizeof(data));
    packet()->cmd = cmd; packet()->size = size; packet()->cmd_id = id;
}
static void dispatch(void) { process_packet(packet(), packet()->size); }
static void bind(uint32_t session, uint32_t id) {
    init_packet(AICA_CMD_OBSERVATION_BIND, 9, id); data[8] = session; dispatch();
}
static aica_observation_control_t *control(uint32_t token, uint32_t ch) {
    init_packet(AICA_CMD_OBSERVATION_CONTROL, 52, token);
    aica_observation_control_t *c = (void *)packet()->cmd_data;
    c->session = 7; c->token = token; c->result_slot = ch < 64 ? ch : 0;
    c->channel = ch; c->operation = AICA_CHANNEL_OP_STOP; return c;
}
static void ack(uint32_t token, uint32_t slot, uint32_t cookie) {
    init_packet(AICA_CMD_OBSERVATION_CONSUME, 11, cookie);
    data[8] = 7; data[9] = slot; data[10] = token; dispatch();
}
static void reset(void) {
    memset(legacy, 0, sizeof(legacy)); memset(statuses, 0, sizeof(statuses));
    chans = legacy; channel_statuses = statuses;
    observer.header = &header; observer.results = results;
    observer.channels = observations;
    observer.completion_header = &completion_header;
    observer.completions = completions;
    lp_reads = 0; memset(lp_bits, 0, sizeof(lp_bits));
    aica_observer_init(&observer); aica_observer_ready(&observer);
    test_clock = 42; writes = reads = commands_rejected = 0;
}
int main(void) {
    reset(); bind(7, 1); CHECK(header.active_session == 7);
    bind(7, 2); CHECK(header.bind_result == AICA_OBSERVATION_BIND_OK);
    bind(8, 3); CHECK(header.active_session == 7 && header.bind_result == AICA_OBSERVATION_BIND_CONFLICT);
    bind(0, 4); CHECK(commands_rejected == 2);
    control(10, 0); dispatch(); CHECK(writes == 1 && results[0].result == AICA_OBSERVATION_APPLIED);
    CHECK(observations[0].attribution_token == 10);
    dispatch(); CHECK(writes == 1); /* retained duplicate */
    control(11, 0); dispatch(); CHECK(writes == 1 && results[0].token == 10);
    ack(9, 0, 12); CHECK(results[0].token == 10 && results[0].ack_result == AICA_OBSERVATION_ACK_MISMATCH);
    ack(10, 0, 13); CHECK(!results[0].token && results[0].high_water_token == 10);
    ack(10, 0, 14); CHECK(results[0].ack_result == AICA_OBSERVATION_ACK_OK && results[0].ack_cookie == 14);
    control(10, 0); dispatch(); CHECK(writes == 1 && results[0].result == AICA_OBSERVATION_STALE_TOKEN);
    ack(10, 0, 15);
    control(16, 64); dispatch(); CHECK(writes == 1 && results[0].result == AICA_OBSERVATION_BAD_CONFIG);
    ack(16, 0, 17);
    aica_observation_control_t *c = control(18, 0); c->operation = 99; dispatch();
    CHECK(results[0].result == AICA_OBSERVATION_BAD_OPERATION); ack(18, 0, 19);
    c = control(20, 0); c->fields = 1; dispatch(); CHECK(results[0].result == AICA_OBSERVATION_BAD_FLAGS); ack(20, 0, 21);
    c = control(22, 0); c->operation = AICA_CHANNEL_OP_UPDATE; c->fields = AICA_CHANNEL_UPDATE_VOLUME;
    dispatch(); CHECK(results[0].result == AICA_OBSERVATION_UNCONFIGURED); ack(22, 0, 23);
    c = control(24, 0); c->operation = AICA_CHANNEL_OP_START; dispatch();
    CHECK(results[0].result == AICA_OBSERVATION_BAD_CONFIG); ack(24, 0, 25);
    control(26, 0); dispatch();
    init_packet(AICA_CMD_OBSERVATION_SAMPLE, 10, 27); data[8] = 7; dispatch();
    CHECK(observations[0].challenge_cookie == 27 && observations[0].position == 123);
    aica_observer_refresh(&observer, 0, 456, 1, UINT32_MAX, 0);
    CHECK(observations[0].challenge_cookie == 27 && observations[0].observation_tick == UINT32_MAX);
    aica_observer_refresh(&observer, 0, 457, 0, 0, 0);
    CHECK(observations[0].observation_tick == 0 && results[0].token == 26);
    /* Malformed/unroutable messages must not touch hardware or retained results. */
    unsigned before = writes, rejected = commands_rejected;
    control(28, 1); packet()->size = 51; dispatch();
    control(28, 1); packet()->misc[3] = 1; dispatch();
    control(28, 1); packet()->cmd_id = 0; dispatch();
    c = control(28, 1); c->session = 8; dispatch();
    c = control(28, 1); c->result_slot = 64; dispatch();
    CHECK(writes == before && commands_rejected == rejected + 5 && !results[1].token);
    /* Actual legacy copied-packet dispatch, including the scalar/mask guard. */
    const uint32_t ops[] = { AICA_CH_CMD_STOP, AICA_CH_CMD_UPDATE };
    for(unsigned i = 0; i < 2; ++i) {
        init_packet(AICA_CMD_CHAN, AICA_CMDSTR_CHANNEL_SIZE, 64);
        aica_channel_t *ch = (void *)packet()->cmd_data;
        ch->cmd = ops[i] | AICA_CH_START_SYNC;
        if(ops[i] == AICA_CH_CMD_UPDATE) ch->cmd |= AICA_CH_UPDATE_SET_VOL;
        unsigned w = writes, r = reads, bad = commands_rejected;
        dispatch(); CHECK(writes == w && reads == r && commands_rejected == bad + 1);
        packet()->cmd_id = 63; dispatch();
        CHECK(observations[63].attribution_kind == AICA_OBSERVATION_ATTR_LEGACY);
    }
    init_packet(AICA_CMD_CHAN, AICA_CMDSTR_CHANNEL_SIZE, 0);
    ((aica_channel_t *)packet()->cmd_data)->cmd = AICA_CH_CMD_UPDATE;
    before = writes; dispatch();
    CHECK(writes == before && observations[0].attribution_token == 26);
    ((aica_channel_t *)packet()->cmd_data)->cmd = AICA_CH_CMD_NONE;
    dispatch(); CHECK(observations[0].attribution_token == 26);
    init_packet(AICA_CMD_CHAN, AICA_CMDSTR_CHANNEL_SIZE, 1u << 31);
    ((aica_channel_t *)packet()->cmd_data)->cmd = AICA_CH_CMD_START | AICA_CH_START_SYNC;
    dispatch(); CHECK(selected_low == (1u << 31) && !selected_high);
    CHECK(observations[31].attribution_kind == AICA_OBSERVATION_ATTR_LEGACY);
    before = writes; rejected = commands_rejected; packet()->cmd_id = 0; dispatch();
    CHECK(writes == before && commands_rejected == rejected + 1);
    init_packet(AICA_CMD_SYNC_CHANNELS, AICA_CMDSTR_CHANNEL_MASK_SIZE, 0);
    data[8] = 1; data[9] = 1u << 31; dispatch();
    CHECK(observations[0].attribution_kind == AICA_OBSERVATION_ATTR_LEGACY);
    CHECK(!observations[0].attribution_token && results[0].token == 26);
    init_packet(AICA_CMD_CHANNEL_CONTROL, AICA_CMDSTR_CHANNEL_CONTROL_SIZE, 1);
    ((aica_channel_control_t *)packet()->cmd_data)->operation = AICA_CHANNEL_OP_STOP;
    dispatch(); CHECK(observations[1].attribution_kind == AICA_OBSERVATION_ATTR_LEGACY);
    /* Every accepted setter invalidates, including a same-value setter;
       rejected updates preserve both attribution and the retained result. */
    const uint32_t setters[] = { AICA_CH_UPDATE_SET_FREQ, AICA_CH_UPDATE_SET_VOL,
                                 AICA_CH_UPDATE_SET_PAN };
    for(unsigned i = 0; i < 3; ++i) {
        observations[0].attribution_kind = AICA_OBSERVATION_ATTR_MANAGED;
        observations[0].attribution_token = 26;
        init_packet(AICA_CMD_CHAN, AICA_CMDSTR_CHANNEL_SIZE, 0);
        aica_channel_t *ch = (void *)packet()->cmd_data;
        ch->cmd = AICA_CH_CMD_UPDATE | setters[i];
        ch->freq = 22050; ch->vol = 100; ch->pan = 128;
        dispatch();
        CHECK(observations[0].attribution_kind == AICA_OBSERVATION_ATTR_LEGACY &&
              !observations[0].attribution_token && results[0].token == 26);
        observations[0].attribution_kind = AICA_OBSERVATION_ATTR_MANAGED;
        observations[0].attribution_token = 26;
        ch->freq = 0; ch->vol = 256; ch->pan = 256;
        before = writes; rejected = commands_rejected; dispatch();
        CHECK(writes == before && commands_rejected == rejected + 1 &&
              observations[0].attribution_token == 26 && results[0].token == 26);
    }
    /* START configuration goes through the real ARM validator and status path. */
    c = control(29, 4); c->operation = AICA_CHANNEL_OP_START;
    c->config.base = AICA_RAM_START; c->config.length = c->config.loopend = 100;
    c->config.freq = 22050; c->config.vol = 100; c->config.pan = 128;
    dispatch(); CHECK(results[4].result == AICA_OBSERVATION_APPLIED && statuses[4].configured);
    /* R1: actual compiler-instrumented validators run once per ARM command. */
    for(unsigned observed = 0; observed < 2; ++observed) {
        unsigned ch = 5 + observed;
        if(observed) {
            c = control(40, ch);
            c->operation = AICA_CHANNEL_OP_START;
        }
        else {
            init_packet(AICA_CMD_CHANNEL_CONTROL, AICA_CMDSTR_CHANNEL_CONTROL_SIZE, ch);
        }
        aica_channel_control_t *legacy_control = (void *)packet()->cmd_data;
        aica_channel_config_t *cfg = observed ? &c->config : &legacy_control->config;
        if(!observed) legacy_control->operation = AICA_CHANNEL_OP_START;
        cfg->base = AICA_RAM_START; cfg->length = cfg->loopend = 100;
        cfg->freq = 22050; cfg->vol = 100; cfg->pan = 128;
        full_validations = field_validations = 0; before = writes; dispatch();
        CHECK(full_validations == 1 && field_validations == 1 && writes == before + 1);
        if(observed) {
            CHECK(results[ch].result == AICA_OBSERVATION_APPLIED); ack(40, ch, 41);
            c = control(42, ch); c->operation = AICA_CHANNEL_OP_UPDATE;
            c->fields = AICA_CHANNEL_UPDATE_VOLUME; c->config.vol = 100;
        }
        else {
            legacy_control->operation = AICA_CHANNEL_OP_UPDATE;
            legacy_control->fields = AICA_CHANNEL_UPDATE_VOLUME;
        }
        full_validations = field_validations = 0; before = writes; dispatch();
        CHECK(full_validations == 0 && field_validations == 1 && writes == before + 1);
        if(observed) {
            CHECK(results[ch].result == AICA_OBSERVATION_APPLIED); ack(42, ch, 43);
            c = control(44, ch); c->operation = AICA_CHANNEL_OP_UPDATE;
            c->fields = AICA_CHANNEL_UPDATE_VOLUME; c->config.vol = 256;
        }
        else legacy_control->config.vol = 256;
        full_validations = field_validations = 0; before = writes; rejected = commands_rejected; dispatch();
        CHECK(full_validations == 0 && field_validations == 1 && writes == before &&
              commands_rejected == rejected + 1);
        if(observed) CHECK(results[ch].result == AICA_OBSERVATION_BAD_CONFIG);
    }
    /* R2: one position/key read per periodic sweep, regardless of binding. */
    for(unsigned bound_session = 0; bound_session < 2; ++bound_session) {
        header.active_session = bound_session ? 7 : 0;
        position_reads = key_reads = 0;
        for(unsigned ch = 0; ch < 64; ++ch) refresh_channel(ch);
        CHECK(position_reads == 64 && key_reads == 64);
        CHECK(observations[63].position == 123 && observations[63].key_on == 0);
        init_packet(AICA_CMD_CHAN, AICA_CMDSTR_CHANNEL_SIZE, 0);
        ((aica_channel_t *)packet()->cmd_data)->cmd = AICA_CH_CMD_STOP;
        unsigned positions = position_reads, keys = key_reads;
        dispatch();
        CHECK(position_reads == positions && key_reads == keys + 1);
    }
    init_packet(AICA_CMD_OBSERVATION_SAMPLE, 10, 50); data[8] = 7; data[9] = 0;
    position_reads = key_reads = 0; dispatch();
    CHECK(position_reads == 1 && key_reads == 1 && observations[0].challenge_cookie == 50);
    aica_observation_channel_t sampled = observations[0];
    test_clock += 10; position_reads = key_reads = 0;
    aica_observer_legacy(&observer, 0);
    CHECK(!position_reads && !key_reads && observations[0].position == sampled.position &&
          observations[0].key_on == sampled.key_on &&
          observations[0].challenge_cookie == sampled.challenge_cookie &&
          observations[0].observation_tick == sampled.observation_tick && results[0].token == 26);
    init_packet(AICA_CMD_OBSERVATION_SAMPLE, 10, 51); data[8] = 7; data[9] = 0;
    dispatch();
    CHECK(position_reads == 1 && key_reads == 1 && observations[0].challenge_cookie == 51 &&
          observations[0].observation_tick == test_clock && test_clock > sampled.observation_tick);
    /* Actual dispatcher/observer: new identity precedes the first LP sample. */
    reset(); bind(7, 1); test_clock = 0; lp_on_start = 1;
    c = control(100, 0); c->operation = AICA_CHANNEL_OP_START;
    c->config.base = AICA_RAM_START; c->config.length = c->config.loopend = 100;
    c->config.freq = 22050;
    dispatch();
    CHECK(completions[0].start_token == 100 &&
          completions[0].flags == (AICA_COMPLETION_ELIGIBLE | AICA_COMPLETION_END_SEEN) &&
          completions[0].first_end_tick == 0 && lp_reads == 1);
    aica_completion_channel_t ended = completions[0];
    dispatch(); CHECK(completions[0].sequence == ended.sequence && lp_reads == 1);
    ack(100, 0, 101); CHECK(completions[0].flags == ended.flags);
    lp_bits[0] = 1; refresh_channel(0);
    CHECK(lp_reads == 1 && lp_bits[0] == 1); /* latched end needs no more LP reads */
    c = control(102, 0); c->operation = AICA_CHANNEL_OP_UPDATE;
    c->fields = AICA_CHANNEL_UPDATE_VOLUME; c->config.vol = 50; dispatch();
    CHECK(completions[0].start_token == 100 && completions[0].command_token == 102 &&
          completions[0].flags == ended.flags && lp_reads == 1);
    ack(102, 0, 103);
    c = control(104, 0); c->operation = AICA_CHANNEL_OP_START;
    dispatch(); CHECK(results[0].result == AICA_OBSERVATION_BAD_CONFIG &&
                      completions[0].start_token == 100);
    ack(104, 0, 105);
    lp_on_start = 0;
    for(unsigned ch = 0; ch < 2; ++ch) {
        c = control(110 + ch, ch); c->operation = AICA_CHANNEL_OP_START;
        c->config.base = AICA_RAM_START; c->config.length = c->config.loopend = 100;
        c->config.freq = 22050; dispatch(); ack(110 + ch, ch, 120 + ch);
    }
    CHECK(completions[0].start_token == 110 && completions[0].flags == AICA_COMPLETION_ELIGIBLE);
    lp_bits[1] = 1; test_clock = 12; refresh_channel(0);
    CHECK(!(completions[0].flags & AICA_COMPLETION_END_SEEN) && lp_bits[1]);
    refresh_channel(1); CHECK(completions[1].flags & AICA_COMPLETION_END_SEEN);
    CHECK(completions[1].first_end_tick == 12 && !lp_bits[1]);
    /* STOP and legacy invalidation make later LP ineligible, not a completion. */
    lp_bits[0] = 1; unsigned old_lp = lp_reads;
    control(130, 0); dispatch(); CHECK(lp_reads == old_lp &&
        completions[0].state == AICA_COMPLETION_STOPPED && !completions[0].flags);
    ack(130, 0, 131);
    c = control(132, 0); c->operation = AICA_CHANNEL_OP_START;
    c->config.base = AICA_RAM_START; c->config.length = c->config.loopend = 100;
    c->config.freq = 22050; c->start_flags = AICA_CHANNEL_START_DELAYED;
    lp_on_start = 1; dispatch(); CHECK(!completions[0].flags && lp_reads == old_lp);
    init_packet(AICA_CMD_SYNC_CHANNELS, AICA_CMDSTR_CHANNEL_MASK_SIZE, 0);
    data[8] = 1; dispatch(); refresh_channel(0);
    CHECK(completions[0].state == AICA_COMPLETION_INTERFERED && !completions[0].flags &&
          lp_reads == old_lp);
    ack(132, 0, 133);
    for(unsigned mode = 0; mode < 3; ++mode) {
        c = control(140 + mode * 2, 0); c->operation = AICA_CHANNEL_OP_START;
        c->config.base = AICA_RAM_START; c->config.length = c->config.loopend = 100;
        c->config.freq = 22050; c->config.loop = mode == 0;
        c->config.type = mode == 0 ? AICA_SM_16BIT : mode;
        dispatch(); CHECK(!completions[0].flags && lp_reads == old_lp);
        ack(140 + mode * 2, 0, 141 + mode * 2);
    }
    completions[0].sequence = UINT32_MAX - 1; refresh_channel(0);
    CHECK(completions[0].sequence == UINT32_MAX);
    completion_header.sequence = UINT32_MAX - 1; bind(7, 150);
    CHECK(completion_header.sequence == UINT32_MAX);
    lp_on_start = 0;
    /* Terminal publication never wraps or silently executes a new control. */
    results[2].sequence = UINT32_MAX - 1; before = writes;
    control(30, 2); dispatch(); CHECK(results[2].sequence == UINT32_MAX && writes == before);
    observations[3].sequence = UINT32_MAX - 1;
    control(31, 3); dispatch(); CHECK(results[3].result == AICA_OBSERVATION_APPLIED);
    CHECK(observations[3].sequence == UINT32_MAX); /* execution can precede failed observation */
    header.sequence = UINT32_MAX - 1; bind(7, 32); CHECK(header.sequence == UINT32_MAX);
    printf("ARM dispatch: %u checks passed\n", checks);
    return 0;
}
