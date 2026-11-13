/* Actual port + playback + P1a + ARM observer; shared OS/transport-only fixture. */
#define PLAYBACK_TEST
#define main observation_suite_main
#include "../sound-observation-test/host.c"
#undef main
#include "../../kernel/arch/dreamcast/sound/snd_channel.c"
#include "../../kernel/arch/dreamcast/sound/snd_port.c"
#include "../../kernel/arch/dreamcast/sound/snd_playback.c"

static snd_channel_config_t config;
static void pb_test_reset(void) {
    /* Test world reset only: never a proposed runtime recovery/free API. */
    memset(ports, 0, sizeof(ports)); memset(pb_states, 0, sizeof(pb_states));
    sfx_inuse = 0; session_counter = 0; apply_result = AICA_OBSERVATION_APPLIED;
    reset(); memset(&config, 0, sizeof(config));
    config.sample_address = AICA_RAM_START; config.sample_count = 100;
    config.loop_end = 100; config.sample_rate = 22050; config.volume = 100;
    config.pan = 128;
}
static snd_port_t new_port(void) {
    snd_port_t p; CHECK(snd_port_reserve(1, &p) == 0);
    CHECK(snd_port_playback_attach(p) == 0); return p;
}
/* Protocol fixtures allow host scheduling time. Explicit deadline tests below
   retain their short budgets; production admission deadlines are unchanged. */
#define PROTOCOL_WAIT_MS 1000u
static void settle_at(snd_port_operation_t *op, const char *file, unsigned line) {
    snd_channel_observation_t o;
    uint64_t began = timer_ms_gettime64();
    int rc = snd_port_playback_poll(op, PROTOCOL_WAIT_MS, &o), error = errno;
    if(rc != 0)
        fprintf(stderr, "settle poll caller=%s:%u port=%u session=%u token=%u operation=%u "
                "expected_rc=0 actual_rc=%d errno=%d elapsed_ms=%llu budget_ms=%u\n",
                file, line, op->port, op->session, op->token,
                pb_states[SND_PORT_SLOT_INDEX(op->port)].operation, rc, error,
                (unsigned long long)(timer_ms_gettime64() - began), PROTOCOL_WAIT_MS);
    errno = error; CHECK(rc == 0);
    began = timer_ms_gettime64();
    rc = snd_port_playback_consume(op, PROTOCOL_WAIT_MS); error = errno;
    if(rc != 0)
        fprintf(stderr, "settle consume caller=%s:%u port=%u session=%u token=%u "
                "expected_rc=0 actual_rc=%d errno=%d elapsed_ms=%llu budget_ms=%u\n",
                file, line, op->port, op->session, op->token, rc, error,
                (unsigned long long)(timer_ms_gettime64() - began), PROTOCOL_WAIT_MS);
    errno = error; CHECK(rc == 0);
}
#define settle(op) settle_at(op, __FILE__, __LINE__)
#include "completion.inc"

struct attach_call { snd_port_t port; int result, error; atomic_bool *go; bool close; };
static void *attach_or_close(void *arg) {
    struct attach_call *c = arg;
    if(c->go) while(!*c->go) nap(1);
    c->result = c->close ? snd_port_close(c->port) : snd_port_playback_attach(c->port);
    c->error = errno; return NULL;
}
int main(void) {
    snd_port_operation_t op, saved = {99,88,77}, stale;
    snd_channel_observation_t observation, unchanged;
    memset(&unchanged, 0xa5, sizeof(unchanged));
    pb_test_reset(); snd_port_t p = new_port(); unsigned idx = SND_PORT_SLOT_INDEX(p);
    CHECK(snd_port_close(p) == -1 && errno == EBUSY);
    CHECK(snd_port_playback_attach(p) == -1 && errno == EBUSY);
    CHECK(snd_port_playback_update(p, &config, SND_CHANNEL_UPDATE_VOLUME, &op) == -1 && errno == EINVAL);
    CHECK(snd_port_playback_start(p, &config, 8, &op) == 1 && ports[idx].pending && !ports[idx].active);
    CHECK(snd_port_playback_detach(p) == -1 && errno == EBUSY);
    CHECK(snd_port_playback_consume(&op, 10) == -1 && errno == EBUSY);
    paused = true; observation = unchanged;
    CHECK(snd_port_playback_poll(&op, 5, &observation) == -1 && errno == ETIMEDOUT &&
          !memcmp(&observation, &unchanged, sizeof(observation)) && ports[idx].pending);
    paused = false;
    CHECK(snd_port_playback_poll(&op, PROTOCOL_WAIT_MS, &observation) == 0 && ports[idx].priority == 8 &&
          ports[idx].active && !ports[idx].pending && pb_states[idx].control == PB_ACTIVE);
    unsigned serial = ports[idx].serial;
    CHECK(snd_port_playback_poll(&op, PROTOCOL_WAIT_MS, &observation) == 0 && ports[idx].serial == serial && ports[idx].priority == 8);
    paused = true;
    CHECK(snd_port_playback_consume(&op, 5) == -1 && errno == ETIMEDOUT && pb_states[idx].receipt.token == op.token);
    CHECK(snd_port_playback_poll(&op, 20, &observation) == -1 && errno == EBUSY);
    paused = false; CHECK(snd_port_playback_consume(&op, PROTOCOL_WAIT_MS) == 0);
    CHECK(snd_port_playback_poll(&op, 20, &observation) == -1 && errno == EINVAL);
    /* key_on is0 in every fake sample: no inferred natural-end priority reset. */
    saved = (snd_port_operation_t){99,88,77}; op = saved;
    CHECK(snd_port_playback_start(p, &config, 7, &op) == 0 && !memcmp(&op, &saved, sizeof(op)) && ports[idx].priority == 8);
    refuse = true;
    CHECK(snd_port_playback_start(p, &config, 9, &op) == -1 && errno == EAGAIN &&
          !ports[idx].pending && ports[idx].priority == 8 && !memcmp(&op, &saved, sizeof(op)));
    refuse = false;
    /* A rejected replacement/UPDATE retains the old managed attribution token. */
    uint32_t previous = pb_states[idx].last_applied;
    apply_result = AICA_OBSERVATION_BAD_CONFIG;
    CHECK(snd_port_playback_start(p, &config, 9, &op) == 1);
    CHECK(snd_port_playback_poll(&op, PROTOCOL_WAIT_MS, &observation) == 0 && observation.result == SND_OBSERVATION_BAD_CONFIG &&
          pb_states[idx].control == PB_ACTIVE && pb_states[idx].last_applied == previous && ports[idx].priority == 8);
    CHECK(snd_port_playback_poll(&op, PROTOCOL_WAIT_MS, &observation) == 0); CHECK(snd_port_playback_consume(&op, PROTOCOL_WAIT_MS) == 0);
    config.volume = 120;
    CHECK(snd_port_playback_update(p, &config, SND_CHANNEL_UPDATE_VOLUME, &op) == 0); settle(&op);
    CHECK(pb_states[idx].committed.volume == 100 && pb_states[idx].control == PB_ACTIVE);
    apply_result = AICA_OBSERVATION_APPLIED;
    CHECK(snd_port_playback_update(p, &config, SND_CHANNEL_UPDATE_VOLUME, &op) == 0); settle(&op);
    CHECK(pb_states[idx].committed.volume == 120);
    /* Historical APPLIED then legacy: commit history but block detach/START. */
    CHECK(snd_port_playback_start(p, &config, 9, &op) == 1); legacy_after_sample = true;
    CHECK(snd_port_playback_poll(&op, PROTOCOL_WAIT_MS, &observation) == 0 && ports[idx].priority == 9 &&
          pb_states[idx].control == PB_INTERFERED);
    CHECK(snd_port_playback_consume(&op, PROTOCOL_WAIT_MS) == 0);
    CHECK(snd_port_playback_start(p, &config, 10, &op) == -1 && errno == EBUSY);
    CHECK(snd_port_playback_detach(p) == -1 && errno == EBUSY);
    CHECK(snd_port_playback_stop(p, &op) == 0); legacy_after_sample = true;
    settle(&op); CHECK(pb_states[idx].control == PB_INTERFERED && !ports[idx].active);
    CHECK(snd_port_playback_detach(p) == -1 && errno == EBUSY);
    CHECK(snd_port_playback_stop(p, &op) == 0); stale = op; settle(&op);
    CHECK(pb_states[idx].control == PB_STOPPED);
    CHECK(snd_port_playback_detach(p) == 0 && snd_port_close(p) == 0);
    snd_port_t replacement; CHECK(snd_port_reserve(1, &replacement) == 0 && replacement != p);
    CHECK(snd_port_playback_attach(p) == -1 && errno == EINVAL);
    CHECK(snd_port_playback_attach(replacement) == 0);
    CHECK(snd_port_playback_poll(&stale, 10, &observation) == -1 && errno == EINVAL);
    CHECK(snd_port_playback_detach(replacement) == 0 && snd_port_close(replacement) == 0);
    /* Every documented active priority pair; observed key-off never clears it. */
    for(unsigned current = 0; current < 16; ++current) for(unsigned incoming = 0; incoming < 16; ++incoming) {
        pb_test_reset(); p = new_port(); idx = SND_PORT_SLOT_INDEX(p);
        CHECK(snd_port_playback_start(p, &config, current, &op) == 1); settle(&op);
        int admit = !current || !incoming || incoming >= current;
        CHECK(snd_port_playback_start(p, &config, incoming, &op) == admit);
        if(admit) settle(&op);
        CHECK(ports[idx].priority == (admit ? incoming : current));
    }
    /* Attach vs close is one generation-checked transition. */
    for(unsigned attempt = 0; attempt < 32; ++attempt) {
        pb_test_reset(); CHECK(snd_port_reserve(1, &p) == 0);
        atomic_bool go = false;
        struct attach_call calls[2] = {{p,0,0,&go,false},{p,0,0,&go,true}};
        pthread_t ts[2];
        for(unsigned i = 0; i < 2; ++i) assert(!pthread_create(&ts[i], NULL, attach_or_close, &calls[i]));
        go = true; for(unsigned i = 0; i < 2; ++i) pthread_join(ts[i], NULL);
        CHECK((calls[0].result == 0 && calls[1].result == -1 && calls[1].error == EBUSY) ||
              (calls[1].result == 0 && calls[0].result == -1 && calls[0].error == EINVAL));
    }
    pb_test_reset(); CHECK(snd_port_reserve(1, &p) == 0);
    struct holder h1, h2;
    pthread_t t1 = start_holder(&h1, &pb_mutex[SND_PORT_SLOT_INDEX(p)], 80), t2;
    struct attach_call delayed = {p,0,0,NULL,false};
    assert(!pthread_create(&t2, NULL, attach_or_close, &delayed));
    CHECK(snd_port_close(p) == 0 && snd_port_reserve(1, &replacement) == 0);
    pthread_join(t1, NULL); pthread_join(t2, NULL);
    CHECK(delayed.result == -1 && delayed.error == EINVAL && !ports[SND_PORT_SLOT_INDEX(replacement)].attached);
    CHECK(snd_port_playback_attach(replacement) == 0); p = replacement; idx = SND_PORT_SLOT_INDEX(p);
    /* Default attach/detach and nested submit waits remain finite. */
    t1 = start_holder(&h1, &pb_mutex[idx], 160);
    CHECK(snd_port_playback_detach(p) == -1 && errno == ETIMEDOUT); pthread_join(t1, NULL);
    t1 = start_holder(&h1, &pb_mutex[idx], 60);
    t2 = start_holder(&h2, &queue_proc_mutex, 180);
    uint64_t began = timer_ms_gettime64();
    CHECK(snd_port_playback_start(p, &config, 8, &op) == -1 && errno == ETIMEDOUT);
    CHECK(timer_ms_gettime64() - began < 160 && !ports[idx].pending);
    pthread_join(t1, NULL); pthread_join(t2, NULL);
    CHECK(snd_port_playback_start(p, &config, 8, &op) == 1);
    t1 = start_holder(&h1, &pb_mutex[idx], 30);
    t2 = start_holder(&h2, &queue_proc_mutex, 140); began = timer_ms_gettime64();
    CHECK(snd_port_playback_poll(&op, 60, &observation) == -1 && errno == ETIMEDOUT);
    CHECK(timer_ms_gettime64() - began < 110 && ports[idx].pending);
    pthread_join(t1, NULL); pthread_join(t2, NULL); settle(&op);
    t1 = start_holder(&h1, &pb_mutex[idx], 160);
    CHECK(snd_port_playback_attach(p) == -1 && errno == ETIMEDOUT);
    pthread_join(t1, NULL);
    config.sample_rate = 0;
    CHECK(snd_port_playback_start(p, &config, 1, &op) == -1 && errno == EINVAL);
    config.sample_rate = 22050;
    in_irq = 1; CHECK(snd_port_playback_stop(p, &op) == -1 && errno == EPERM); in_irq = 0;
    /* Exhaustion and reset preserve attached capacity/obligations. */
    cookie = UINT32_MAX;
    CHECK(snd_port_playback_stop(p, &op) == -1 && errno == EOVERFLOW && ports[idx].attached);
    snd_observation_load_begin();
    CHECK(pb_states[idx].control == PB_LOST && ports[idx].attached);
    CHECK(snd_port_playback_detach(p) == -1 && errno == ENODEV);
    CHECK(snd_port_close(p) == -1 && errno == EBUSY);
    pb_test_reset(); p = new_port(); idx = SND_PORT_SLOT_INDEX(p);
    CHECK(snd_port_playback_start(p, &config, 8, &op) == 1);
    snd_observation_reset();
    CHECK(ports[idx].pending && ports[idx].attached && pb_states[idx].receipt.token == op.token);
    CHECK(snd_port_playback_poll(&op, 10, &observation) == -1 && errno == ENODEV);
    CHECK(snd_port_close(p) == -1 && errno == EBUSY);
    pb_test_reset(); p = new_port(); idx = SND_PORT_SLOT_INDEX(p);
    ports[idx].serial = UINT32_MAX; op = saved;
    CHECK(snd_port_playback_start(p, &config, 8, &op) == -1 && errno == EOVERFLOW &&
          !ports[idx].pending && !memcmp(&op, &saved, sizeof(op)));
    pb_test_reset(); p = new_port(); idx = SND_PORT_SLOT_INDEX(p);
    CHECK(snd_port_playback_start(p, &config, 8, &op) == 1);
    header.active_session = 0; observation = unchanged;
    CHECK(snd_port_playback_poll(&op, 20, &observation) == -1 && errno == ENODEV &&
          pb_states[idx].control == PB_LOST && ports[idx].pending &&
          !memcmp(&observation, &unchanged, sizeof(observation)));
    pb_test_reset(); CHECK(snd_port_reserve(2, &p) == 0);
    CHECK(snd_port_playback_attach(p) == -1 && errno == ENOTSUP && snd_port_close(p) == 0);
    CHECK(snd_port_reserve(1, &p) == 0); session_counter = UINT32_MAX; snd_observation_load_begin();
    CHECK(snd_port_playback_attach(p) == -1 && errno == EOVERFLOW && !ports[SND_PORT_SLOT_INDEX(p)].attached);
    CHECK(!timed_zero);
    completion_tests();
    printf("Managed mono playback: %u checks passed\n", checks);
    return 0;
}
