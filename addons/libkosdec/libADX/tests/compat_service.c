#include <kosdec/adx_compat_service.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do { \
    ++checks; \
    if(!(test)) { \
        fprintf(stderr,"compat-service:%u %s\n",(unsigned)__LINE__,#test); \
        abort(); \
    } \
} while(0)

struct fixture {
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title titles[5];
    uint64_t now;
    unsigned trace[16], events;
    bool tracing;
    bool check_tick;
    uint32_t expected_tick;
};
struct phase {
    struct fixture *fixture;
    unsigned id;
    bool fail;
    bool reenter;
};
struct title {
    struct fixture *fixture;
    enum kosdec_adx_compat_state state, next;
    struct kosdec_adx_compat_observation data;
    unsigned dispatches, observed;
    bool transition, defer, transport;
};

static void trace(struct fixture *f, unsigned id) {
    if(f->check_tick) {
        uint32_t tick;
        CHECK(!kosdec_adx_compat_get_vsync_count(&f->service,&tick) && tick == f->expected_tick);
    }
    if(f->tracing) {
        CHECK(f->events < sizeof(f->trace) / sizeof(*f->trace));
        f->trace[f->events++] = id;
    }
}

static uint64_t clock_us(void *context) {
    struct fixture *f = context;
    return f->now;
}

static int service_phase(void *context) {
    struct phase *p = context;
    trace(p->fixture,10 + p->id);
    if(p->reenter) {
        struct kosdec_adx_compat_result r = {.visited = 91};
        CHECK(kosdec_adx_compat_exec_vsync(&p->fixture->service,&r) < 0 && errno == EBUSY);
        CHECK(r.visited == 91);
    }
    if(p->fail) {
        errno = ENOSPC;
        return -1;
    }
    return 0;
}

static enum kosdec_adx_compat_state state(void *context) {
    struct title *t = context;
    return t->state;
}

static int dispatch(void *context, enum kosdec_adx_compat_state entry) {
    struct title *t = context;
    CHECK(entry == t->state);
    trace(t->fixture,(unsigned)entry);
    ++t->dispatches;
    if(t->transition) {
        t->state = t->next;
        t->transition = false;
    }
    return 0;
}

static int observe(void *context, struct kosdec_adx_compat_observation *out) {
    struct title *t = context;
    if(t->defer) return 1;
    *out = t->data;
    ++t->observed;
    return 0;
}

static bool transport_failed(void *context) {
    const struct title *t = context;
    return t->transport;
}

static const struct kosdec_adx_compat_title_ops ops = {
    .state = state, .dispatch = dispatch, .observe = observe,
    .transport_failed = transport_failed
};

static void init(struct fixture *f) {
    *f = (struct fixture){0};
    const struct kosdec_adx_compat_config config = {
        .clock_us = clock_us, .clock_context = f
    };
    CHECK(!kosdec_adx_compat_service_init(&f->service,f->titles,5,&config));
}

static void prepare(struct fixture *f, struct title *t, unsigned slot,
                    enum kosdec_adx_compat_state start) {
    *t = (struct title){.fixture = f, .state = start};
    CHECK(!kosdec_adx_compat_bind(&f->service,slot,&ops,t));
}

static void service_order(void) {
    struct fixture f = {.tracing = true};
    struct phase phases[KOSDEC_ADX_COMPAT_PHASE_COUNT];
    struct kosdec_adx_compat_config config = {.clock_us = clock_us,.clock_context = &f};
    for(unsigned i = 0; i < KOSDEC_ADX_COMPAT_PHASE_COUNT; ++i) {
        phases[i] = (struct phase){.fixture = &f,.id = i};
        if(i != KOSDEC_ADX_COMPAT_RENDERER)
            config.phases[i] = (struct kosdec_adx_compat_phase_op){
                .service = service_phase, .context = &phases[i]
            };
    }
    CHECK(!kosdec_adx_compat_service_init(&f.service,f.titles,5,&config));
    struct title stop, info, running;
    prepare(&f,&stop,0,KOSDEC_ADX_COMPAT_STOP);
    prepare(&f,&info,2,KOSDEC_ADX_COMPAT_DECINFO);
    prepare(&f,&running,4,KOSDEC_ADX_COMPAT_PLAYING);
    struct kosdec_adx_compat_result r;

    f.now = 13331;
    CHECK(!kosdec_adx_compat_exec_server(&f.service,&r));
    CHECK(r.visited == 0x15 && !r.watchdog_checks);
    const unsigned expected[] = {10,12,13,KOSDEC_ADX_COMPAT_DECINFO,KOSDEC_ADX_COMPAT_PLAYING,14};
    CHECK(f.events == sizeof(expected) / sizeof(*expected) &&
          !memcmp(f.trace,expected,sizeof(expected)));
    CHECK(r.serviced_phases == 0x1d && !r.failed_phases);
    CHECK(!stop.dispatches && !stop.observed && !info.observed && !running.observed);

    f.events = 0;
    ++f.now;
    CHECK(!kosdec_adx_compat_exec_server(&f.service,&r) && r.watchdog_checks == 1);
    CHECK(!stop.observed && !info.observed && !running.observed);
    CHECK(!kosdec_adx_compat_unbind(&f.service,0));
    CHECK(kosdec_adx_compat_exec_title(&f.service,0,&r) == -1 && errno == EINVAL);

    f.events = 0;
    f.now += 13332;
    info.transition = true;
    info.next = KOSDEC_ADX_COMPAT_PLAYING;
    CHECK(!kosdec_adx_compat_exec_server(&f.service,&r) && r.visited == 0x14 &&
          r.watchdog_checks == 4);
    CHECK(info.state == KOSDEC_ADX_COMPAT_PLAYING && info.dispatches == 3 &&
          info.observed == 1 && running.observed == 0);
    CHECK(f.titles[2].stalled_checks == 1 && f.titles[2].low_checks == 1);

    f.events = 0;
    CHECK(!kosdec_adx_compat_exec_title(&f.service,4,&r));
    CHECK(r.visited == 16 && !r.watchdog_checks && !r.serviced_phases);
    CHECK(f.events == 1 && !running.observed);

    struct fixture independent;
    struct title second;
    init(&independent);
    prepare(&independent,&second,3,KOSDEC_ADX_COMPAT_DECINFO);
    independent.now = f.now;
    CHECK(!kosdec_adx_compat_exec_server(&independent.service,&r));
    CHECK(r.visited == 8 && r.watchdog_checks == 8 && second.observed == 1);
    CHECK(!r.failed_phases && !r.serviced_phases);

    second.defer = true;
    independent.now += 13332;
    CHECK(!kosdec_adx_compat_exec_title(&independent.service,3,&r));
    CHECK(r.watchdog_checks == 8 && r.watchdog_deferred == 8 && !r.failed_titles &&
          second.observed == 1 && independent.titles[3].low_checks == 1);

    phases[KOSDEC_ADX_COMPAT_FILES].fail = true;
    running.state = (enum kosdec_adx_compat_state)6;
    f.events = 0;
    CHECK(kosdec_adx_compat_exec_server(&f.service,&r) < 0 && errno == ENOSPC);
    CHECK(r.failed_phases == 4 && r.failed_titles == 16 &&
          r.serviced_phases == 0x19 && r.visited == 0x14);
    CHECK(f.trace[f.events - 1] == 14);
}

static void prefill_targets(void) {
    struct fixture f;
    struct title t;
    init(&f);
    prepare(&f,&t,0,KOSDEC_ADX_COMPAT_DECINFO);
    uint32_t frames;
    CHECK(!kosdec_adx_compat_prefill(&f.service,0,44100,32,false,&frames) && frames == 1152);
    const struct {
        int frequency;
        uint32_t rate, target;
        bool loop;
    } cases[] = {
        {30,44100,2240,false}, {60,44100,1152,false}, {120,44100,576,false},
        {30,44100,4416,true}, {60,44100,2240,true}, {120,44100,1152,true},
        {60,32000,832,false}, {120,46080,640,false}
    };
    for(unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        CHECK(!kosdec_adx_compat_set_frequency(&f.service,0,cases[i].frequency));
        CHECK(!kosdec_adx_compat_prefill(&f.service,0,cases[i].rate,32,cases[i].loop,&frames) &&
              frames == cases[i].target);
    }
    CHECK(!kosdec_adx_compat_set_frequency(&f.service,0,1));
    CHECK(kosdec_adx_compat_prefill(&f.service,0,UINT32_MAX,32,true,&frames) < 0 &&
          errno == EOVERFLOW && frames == 640);
    CHECK(kosdec_adx_compat_set_frequency(&f.service,0,0) < 0 && errno == EINVAL);
}

static void advance(struct fixture *f, unsigned checks_to_run) {
    struct kosdec_adx_compat_result r;
    for(unsigned i = 0; i < checks_to_run; ++i) {
        f->now += 1000000;
        CHECK(!kosdec_adx_compat_exec_title(&f->service,0,&r) && r.watchdog_checks == 1);
    }
}

static enum kosdec_adx_compat_error error(const struct fixture *f) {
    enum kosdec_adx_compat_error code;
    CHECK(!kosdec_adx_compat_get_error(&f->service,0,&code));
    return code;
}

static void watchdog(void) {
    const struct {
        int frequency;
        enum kosdec_adx_compat_state state;
        unsigned bytes, admitted;
        enum kosdec_adx_compat_error code;
    } cases[] = {
        {2,KOSDEC_ADX_COMPAT_DECINFO,63,40,KOSDEC_ADX_COMPAT_LOW_DATA},
        {3,KOSDEC_ADX_COMPAT_PREP,63,60,KOSDEC_ADX_COMPAT_LOW_DATA},
        {2,KOSDEC_ADX_COMPAT_PLAYING,63,10,KOSDEC_ADX_COMPAT_LOW_DATA},
        {3,KOSDEC_ADX_COMPAT_PLAYING,63,15,KOSDEC_ADX_COMPAT_LOW_DATA},
        {2,KOSDEC_ADX_COMPAT_PLAYING,64,10,KOSDEC_ADX_COMPAT_STALLED},
        {3,KOSDEC_ADX_COMPAT_PLAYING,64,15,KOSDEC_ADX_COMPAT_STALLED},
        {2,KOSDEC_ADX_COMPAT_PREP,64,60,KOSDEC_ADX_COMPAT_NO_ERROR}
    };
    for(unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        struct fixture f;
        struct title t;
        init(&f);
        prepare(&f,&t,0,cases[i].state);
        t.data.input_bytes = cases[i].bytes;
        CHECK(!kosdec_adx_compat_set_frequency(&f.service,0,cases[i].frequency));
        advance(&f,cases[i].admitted);
        CHECK(error(&f) == KOSDEC_ADX_COMPAT_NO_ERROR);
        advance(&f,1);
        CHECK(error(&f) == cases[i].code);
        CHECK(!kosdec_adx_compat_clear_error(&f.service,0));
        CHECK(error(&f) == KOSDEC_ADX_COMPAT_NO_ERROR);
    }

    struct fixture f;
    struct title t;
    init(&f);
    prepare(&f,&t,0,KOSDEC_ADX_COMPAT_DECINFO);
    CHECK(!kosdec_adx_compat_set_frequency(&f.service,0,20));
    advance(&f,45);
    CHECK(error(&f) == KOSDEC_ADX_COMPAT_NO_ERROR);
    CHECK(!kosdec_adx_compat_set_frequency(&f.service,0,2));
    advance(&f,1);
    CHECK(error(&f) == KOSDEC_ADX_COMPAT_LOW_DATA);

    CHECK(!kosdec_adx_compat_clear_error(&f.service,0));
    t.state = KOSDEC_ADX_COMPAT_PLAYING;
    advance(&f,10);
    t.data.paused = true;
    advance(&f,1);
    CHECK(error(&f) == KOSDEC_ADX_COMPAT_NO_ERROR &&
          !f.titles[0].stalled_checks && !f.titles[0].low_checks);
    t.data.paused = false;
    advance(&f,10);
    t.data.decoder_done = true;
    advance(&f,1);
    CHECK(error(&f) == KOSDEC_ADX_COMPAT_NO_ERROR &&
          !f.titles[0].stalled_checks && !f.titles[0].low_checks);
    t.data.decoder_done = false;
    t.data.input_bytes = 64;
    advance(&f,10);
    t.data.decoded_position = 1;
    advance(&f,1);
    CHECK(error(&f) == KOSDEC_ADX_COMPAT_NO_ERROR && !f.titles[0].stalled_checks);
}

static void fault_observations(void) {
    struct fixture f;
    struct title t;
    struct kosdec_adx_compat_result r;
    uint32_t faults = 0;
    init(&f);
    prepare(&f,&t,0,KOSDEC_ADX_COMPAT_PLAYING);
    CHECK(!kosdec_adx_compat_set_frequency(&f.service,0,2));
    t.data.input_bytes = 63;
    advance(&f,10);
    CHECK(!kosdec_adx_compat_get_faults(&f.service,0,&faults) && !faults);
    t.transport = true;
    CHECK(!kosdec_adx_compat_exec_title(&f.service,0,&r) && !r.watchdog_checks);
    CHECK(!kosdec_adx_compat_get_faults(&f.service,0,&faults) && !faults);

    advance(&f,1);
    CHECK(!kosdec_adx_compat_get_faults(&f.service,0,&faults));
    CHECK(faults == (KOSDEC_ADX_COMPAT_FAULT_LOW_DATA |
                     KOSDEC_ADX_COMPAT_FAULT_STALLED |
                     KOSDEC_ADX_COMPAT_FAULT_TRANSPORT));
    CHECK(error(&f) == KOSDEC_ADX_COMPAT_LOW_DATA);
    CHECK(!kosdec_adx_compat_clear_error(&f.service,0));
    CHECK(!kosdec_adx_compat_get_faults(&f.service,0,&faults) && !faults);

    t.state = KOSDEC_ADX_COMPAT_STOP;
    unsigned observations = t.observed;
    advance(&f,1);
    CHECK(t.observed == observations && error(&f) == KOSDEC_ADX_COMPAT_NO_ERROR);
    CHECK(!kosdec_adx_compat_get_faults(&f.service,0,&faults) &&
          faults == KOSDEC_ADX_COMPAT_FAULT_TRANSPORT);
    t.state = KOSDEC_ADX_COMPAT_PREP;
    t.defer = true;
    f.now += 1000000;
    CHECK(!kosdec_adx_compat_exec_title(&f.service,0,&r) && r.watchdog_deferred==1);
    CHECK(!kosdec_adx_compat_get_faults(&f.service,0,&faults) &&
          faults == KOSDEC_ADX_COMPAT_FAULT_TRANSPORT);
    CHECK(error(&f) == KOSDEC_ADX_COMPAT_NO_ERROR);
}

static void vsync_pass(void) {
    struct fixture f = {.tracing = true, .check_tick = true};
    struct fixture other;
    struct phase phases[KOSDEC_ADX_COMPAT_PHASE_COUNT];
    struct kosdec_adx_compat_config config = {.clock_us = clock_us,.clock_context = &f};
    for(unsigned i = 0; i < KOSDEC_ADX_COMPAT_PHASE_COUNT; ++i) {
        phases[i] = (struct phase){.fixture = &f,.id = i,.reenter = i == KOSDEC_ADX_COMPAT_DECODER};
        config.phases[i] = (struct kosdec_adx_compat_phase_op){.service=service_phase,.context=&phases[i]};
    }
    CHECK(!kosdec_adx_compat_service_init(&f.service,f.titles,5,&config));
    init(&other);
    struct title stop, info, running;
    prepare(&f,&stop,0,KOSDEC_ADX_COMPAT_STOP);
    prepare(&f,&info,2,KOSDEC_ADX_COMPAT_DECINFO);
    prepare(&f,&running,4,KOSDEC_ADX_COMPAT_PLAYING);
    uint32_t tick = 91;
    CHECK(kosdec_adx_compat_enable_vsync_clock(&f.service) < 0 && errno == EBUSY);
    CHECK(kosdec_adx_compat_enable_vsync_clock(NULL) < 0 && errno == EINVAL);
    CHECK(!kosdec_adx_compat_enable_vsync_clock(&other.service));
    CHECK(kosdec_adx_compat_get_vsync_count(NULL,&tick) < 0 && tick == 91);
    CHECK(!kosdec_adx_compat_get_vsync_count(&f.service,&tick) && tick == 0);
    CHECK(kosdec_adx_compat_exec_vsync(&f.service,NULL) < 0 && errno == EINVAL);
    f.service.vsync_count = UINT32_MAX - 1; /* Seed rollover without billions of passes. */
    f.expected_tick = UINT32_MAX;
    f.now = 13332;
    struct kosdec_adx_compat_result r;
    errno = EDOM;
    CHECK(!kosdec_adx_compat_exec_vsync(&f.service,&r) && errno == EDOM);
    CHECK(r.visited == 0x15 && r.watchdog_checks == 1 && r.serviced_phases == 0x1f);
    const unsigned expected[] = {10,11,12,13,KOSDEC_ADX_COMPAT_DECINFO,KOSDEC_ADX_COMPAT_PLAYING,14};
    CHECK(f.events == sizeof(expected) / sizeof(*expected) && !memcmp(f.trace,expected,sizeof(expected)));

    f.events = 0;
    f.expected_tick = 0;
    phases[1].fail = true;
    CHECK(kosdec_adx_compat_exec_vsync(&f.service,&r)<0 && errno==ENOSPC);
    CHECK(r.visited == 0x15 && !r.watchdog_checks && r.serviced_phases == 0x1d && r.failed_phases == 2);
    CHECK(f.events == sizeof(expected) / sizeof(*expected) && !memcmp(f.trace,expected,sizeof(expected)));
    CHECK(!kosdec_adx_compat_get_vsync_count(&f.service,&tick) && tick == 0);

    f.events = 0;
    phases[1].fail = false;
    CHECK(!kosdec_adx_compat_exec_server(&f.service,&r));
    CHECK(!kosdec_adx_compat_exec_title(&f.service,2,&r) && r.visited == 4);
    CHECK(!kosdec_adx_compat_get_vsync_count(&f.service,&tick) && tick == 0);
    CHECK(!kosdec_adx_compat_exec_vsync(&other.service,&r) && !r.visited && !r.serviced_phases);
    CHECK(!kosdec_adx_compat_get_vsync_count(&other.service,&tick) && tick == 1);
    CHECK(!kosdec_adx_compat_get_vsync_count(&f.service,&tick) && tick == 0);
}

int main(void) {
    service_order();
    prefill_targets();
    watchdog();
    fault_observations();
    vsync_pass();
    printf("ADX_COMPAT_SERVICE checks=%u failures=0\n",checks);
    return 0;
}
