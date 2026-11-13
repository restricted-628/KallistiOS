/* Production SH-4 state machine and bridge bodies connected to ARM core.
   Simulates transport/OS/MMIO only; no alternate observation policy. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <kos/mutex.h>
#include <dc/sound/observation.h>
#include "aica_observer.h"
#include "aica_cmd_iface.h"
static atomic_uint timed_zero;
uint64_t timer_ms_gettime64(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void nap(unsigned ms) {
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000}; nanosleep(&t, NULL);
}
static int lock_impl(mutex_t *m, unsigned timeout) {
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += timeout / 1000; end.tv_nsec += (long)(timeout % 1000) * 1000000;
    if(end.tv_nsec >= 1000000000) { ++end.tv_sec; end.tv_nsec -= 1000000000; }
    pthread_mutex_lock(&m->gate);
    while(m->depth && !(m->recursive && pthread_equal(m->owner, pthread_self()))) {
        int err = timeout ? pthread_cond_timedwait(&m->wake, &m->gate, &end) :
                            pthread_cond_wait(&m->wake, &m->gate);
        if(err) { pthread_mutex_unlock(&m->gate); errno = err; return -1; }
    }
    m->owner = pthread_self(); ++m->depth; pthread_mutex_unlock(&m->gate); return 0;
}
int mutex_lock_timed(mutex_t *m, unsigned timeout) {
    if(!timeout) { ++timed_zero; assert(timeout); }
    return lock_impl(m, timeout);
}
int mutex_lock(mutex_t *m) { return lock_impl(m, 0); }
int mutex_unlock(mutex_t *m) {
    pthread_mutex_lock(&m->gate); assert(m->depth && pthread_equal(m->owner, pthread_self()));
    if(!--m->depth) pthread_cond_broadcast(&m->wake);
    pthread_mutex_unlock(&m->gate); return 0;
}
static pthread_mutex_t irq_gate = PTHREAD_MUTEX_INITIALIZER;
static int in_irq;
int irq_inside_int(void) { return in_irq; }
int irq_disable(void) { pthread_mutex_lock(&irq_gate); return 0; }
void irq_restore(int old) { (void)old; pthread_mutex_unlock(&irq_gate); }
static bool initted = true;
static uint32_t driver_features = SND_DRIVER_FEATURE_OBSERVATION;
bool snd_iface_is_initialized(void) { return initted; }
static mutex_t queue_proc_mutex = MUTEX_INITIALIZER_RECURSIVE;
static pthread_mutex_t bus = PTHREAD_MUTEX_INITIALIZER;
static aica_observation_header_t header;
static aica_completion_header_t completion_header;
static aica_completion_channel_t completions[64];
static uint32_t lp_bits[64], selected_channel, lp_on_start;
static void (*after_sample)(unsigned);
static unsigned lp_reads;
static uint32_t loop_end(void) {
    ++lp_reads; uint32_t v=lp_bits[selected_channel]; lp_bits[selected_channel]=0; return v;
}
static aica_observation_result_t results[64];
static aica_observation_channel_t channels[64];
static unsigned applied;
static uint32_t apply_result = AICA_OBSERVATION_APPLIED;
static uint32_t apply(uint32_t ch, const aica_channel_control_t *c) {
    assert(ch < 64); ++applied;
    if(apply_result == AICA_OBSERVATION_APPLIED && c->operation == AICA_CHANNEL_OP_START)
        lp_bits[ch] = lp_on_start;
    return apply_result;
}
static void sample(uint32_t ch, uint32_t *pos, uint32_t *key) {
    assert(ch < 64); selected_channel = ch; *pos = 123; *key = 0;
}
static aica_observer_t arm = {&header, results, channels, apply, sample,
    &completion_header, completions, loop_end};
static struct { uint32_t words[64]; uint64_t ready; } queue[1024];
static unsigned head, tail, packets;
static unsigned delay_ms, publish_delay_ms;
static bool paused, refuse, legacy_after_sample;
static aica_observation_channel_t before_legacy;
int snd_sh4_to_aica(void *packet, uint32_t words) {
    mutex_lock(&queue_proc_mutex); /* matches legacy recursive transport ownership */
    if(refuse) { mutex_unlock(&queue_proc_mutex); errno = EAGAIN; return -1; }
    pthread_mutex_lock(&bus);
    assert(head - tail < 1024 && words <= 64);
    memcpy(queue[head % 1024].words, packet, words * 4);
    queue[head % 1024].ready = timer_ms_gettime64() + delay_ms; ++head; ++packets;
    pthread_mutex_unlock(&bus);
    if(publish_delay_ms) nap(publish_delay_ms);
    mutex_unlock(&queue_proc_mutex); return 0;
}
static void drain(void) {
    pthread_mutex_lock(&bus);
    while(!paused && tail != head && queue[tail % 1024].ready <= timer_ms_gettime64()) {
        aica_cmd_t *packet = (void *)queue[tail % 1024].words;
        aica_observer_command(&arm, packet, (uint32_t)timer_ms_gettime64());
        if(legacy_after_sample && packet->cmd == AICA_CMD_OBSERVATION_SAMPLE) {
            uint32_t ch = queue[tail % 1024].words[9];
            before_legacy = channels[ch];
            aica_observer_legacy(&arm, ch);
            legacy_after_sample = false;
        }
        if(after_sample && packet->cmd == AICA_CMD_OBSERVATION_SAMPLE)
            after_sample(queue[tail % 1024].words[9]);
        ++tail;
    }
    pthread_mutex_unlock(&bus);
}
void thd_pass(void) { drain(); nap(1); }
#define SPU_RAM_UNCACHED_BASE 0xa0800000u
static void bus_unlock(int *guard) { (void)guard; pthread_mutex_unlock(&bus); }
#define g2_lock_scoped() int guard __attribute__((cleanup(bus_unlock))) = 0; pthread_mutex_lock(&bus)
static void g2_fifo_wait(void) {}
static uint32_t g2_read_32_raw(uint32_t address) {
    uint32_t off = address - SPU_RAM_UNCACHED_BASE;
    if(off >= AICA_MEM_OBSERVATION_HEADER && off < AICA_MEM_OBSERVATION_HEADER + sizeof(header))
        return ((uint32_t *)&header)[(off - AICA_MEM_OBSERVATION_HEADER) / 4];
    if(off >= AICA_MEM_OBSERVATION_RESULTS && off < AICA_MEM_OBSERVATION_CHANNELS)
        return ((uint32_t *)results)[(off - AICA_MEM_OBSERVATION_RESULTS) / 4];
    if(off >= AICA_MEM_COMPLETION_HEADER && off < AICA_MEM_COMPLETION_HEADER + 64)
        return ((uint32_t *)&completion_header)[(off - AICA_MEM_COMPLETION_HEADER) / 4];
    if(off >= AICA_MEM_COMPLETION_CHANNELS && off < AICA_MEM_COMPLETION_END)
        return ((uint32_t *)completions)[(off - AICA_MEM_COMPLETION_CHANNELS) / 4];
    assert(off >= AICA_MEM_OBSERVATION_CHANNELS && off < AICA_MEM_OBSERVATION_END);
    return ((uint32_t *)channels)[(off - AICA_MEM_OBSERVATION_CHANNELS) / 4];
}
#include "bridges.inc"
/* Mach-O spells an optional undefined reference weak_import. This fixture-only
   attribute mapping lets standalone P1a exercise the absent-hook path; production SH-4
   weak/strong archive semantics are checked on actual target objects/ELFs. */
#if defined(__APPLE__) && !defined(PLAYBACK_TEST)
#define weak weak_import
#endif
#include "../../kernel/arch/dreamcast/sound/snd_observation.c"
#if defined(__APPLE__) && !defined(PLAYBACK_TEST)
#undef weak
#endif
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #x, errno); abort(); } } while(0)
static void reset(void) {
    aica_observer_init(&arm); aica_observer_ready(&arm); snd_observation_load_begin();
    memset(lp_bits, 0, sizeof(lp_bits)); lp_reads = lp_on_start = 0; after_sample = NULL;
    head = tail = packets = applied = 0; delay_ms = publish_delay_ms = 0; paused = refuse = false;
    initted = true; driver_features = SND_DRIVER_FEATURE_OBSERVATION;
}
static int submit(unsigned ch, snd_channel_receipt_t *r) {
    return snd_channel_observed_submit(ch, SND_OBSERVATION_STOP, NULL, 0, 0, r);
}
struct caller { unsigned channel; int result; snd_channel_receipt_t receipt; };
static void *first_use(void *arg) {
    struct caller *c = arg; c->result = submit(c->channel, &c->receipt); return NULL;
}
struct holder { mutex_t *mutex; atomic_bool locked; unsigned duration; };
static void *hold(void *arg) {
    struct holder *h = arg; mutex_lock(h->mutex); h->locked = true;
    nap(h->duration); mutex_unlock(h->mutex); return NULL;
}
static pthread_t start_holder(struct holder *h, mutex_t *m, unsigned duration) {
    h->mutex = m; h->locked = false; h->duration = duration;
    pthread_t thread; assert(!pthread_create(&thread, NULL, hold, h));
    while(!h->locked) nap(1); return thread;
}
int main(void) {
    snd_channel_receipt_t r, saved_r = {99, 88, 77};
    snd_channel_observation_t o, saved_o; memset(&saved_o, 0xa5, sizeof(saved_o));
    reset(); delay_ms = 15;
    struct caller callers[2] = {{.channel=0}, {.channel=1}};
    pthread_t threads[2];
    for(unsigned i = 0; i < 2; ++i) assert(!pthread_create(&threads[i], NULL, first_use, &callers[i]));
    for(unsigned i = 0; i < 2; ++i) pthread_join(threads[i], NULL);
    CHECK(!callers[0].result && !callers[1].result && packets == 3);
    CHECK(callers[0].receipt.session == callers[1].receipt.session);
    CHECK(callers[0].receipt.token != callers[1].receipt.token);
    r = callers[0].receipt; delay_ms = 0;
    CHECK(snd_channel_observed_consume(&r, 20) == -1 && errno == EBUSY);
    CHECK(snd_channel_observed_poll(&r, 100, &o) == 0 && o.result == SND_OBSERVATION_APPLIED);
    CHECK(o.attribution_token == r.token && o.host_observed_ms >= o.host_challenge_started_ms);
    saved_r = r; CHECK(submit(0, &r) == -1 && errno == EBUSY && !memcmp(&r, &saved_r, sizeof(r)));
    /* ACK may execute after timeout. Retry must obtain a fresh idempotent ACK. */
    paused = true;
    CHECK(snd_channel_observed_consume(&r, 5) == -1 && errno == ETIMEDOUT && slots[0].token == r.token);
    o = saved_o; CHECK(snd_channel_observed_poll(&r, 10, &o) == -1 && errno == EBUSY && !memcmp(&o, &saved_o, sizeof(o)));
    paused = false; drain(); CHECK(!results[0].token && slots[0].token == r.token);
    CHECK(snd_channel_observed_consume(&r, 50) == 0 && !slots[0].token);
    CHECK(snd_channel_observed_poll(&r, 10, &o) == -1 && errno == EINVAL);
    /* Refusal burns identity but cannot publish a receipt. */
    refuse = true; r = saved_r; uint32_t old_cookie = cookie;
    CHECK(submit(2, &r) == -1 && errno == EAGAIN && cookie == old_cookie + 1);
    CHECK(!slots[2].token && !memcmp(&r, &saved_r, sizeof(r))); refuse = false;
    paused = true; CHECK(submit(2, &r) == 0); o = saved_o;
    CHECK(snd_channel_observed_poll(&r, 5, &o) == -1 && errno == ETIMEDOUT);
    CHECK(slots[2].token == r.token && !memcmp(&o, &saved_o, sizeof(o)));
    paused = false; CHECK(snd_channel_observed_poll(&r, 50, &o) == 0);
    paused = true; o = saved_o;
    CHECK(snd_channel_observed_poll(&r, 5, &o) == -1 && errno == ETIMEDOUT &&
          !memcmp(&o, &saved_o, sizeof(o))); /* old challenge is not fresh */
    paused = false;
    aica_observer_legacy(&arm, 2);
    CHECK(snd_channel_observed_poll(&r, 50, &o) == 0 && o.attribution_kind == SND_OBSERVATION_ATTR_LEGACY && o.result == SND_OBSERVATION_APPLIED);
    legacy_after_sample = true;
    CHECK(snd_channel_observed_poll(&r, 50, &o) == 0 && !legacy_after_sample &&
          o.attribution_kind == SND_OBSERVATION_ATTR_LEGACY && !o.attribution_token);
    CHECK(o.position == before_legacy.position && o.key_on == before_legacy.key_on &&
          o.observation_tick == before_legacy.observation_tick &&
          channels[r.channel].challenge_cookie == before_legacy.challenge_cookie);
    nap(2);
    CHECK(snd_channel_observed_poll(&r, 50, &o) == 0 &&
          channels[r.channel].challenge_cookie != before_legacy.challenge_cookie &&
          o.observation_tick > before_legacy.observation_tick);
    CHECK(snd_channel_observed_poll(&r, 0, &o) == -1 && errno == EINVAL);
    CHECK(snd_channel_observed_consume(&r, 0) == -1 && errno == EINVAL);
    /* All software lock waits share the caller's finite deadline. */
    struct holder holder; uint64_t started;
    pthread_t t = start_holder(&holder, &queue_proc_mutex, 180); started = timer_ms_gettime64();
    CHECK(submit(3, &saved_r) == -1 && errno == ETIMEDOUT);
    CHECK(timer_ms_gettime64() - started >= 90 && timer_ms_gettime64() - started < 170);
    pthread_join(t, NULL);
    t = start_holder(&holder, &queue_proc_mutex, 80); started = timer_ms_gettime64();
    CHECK(snd_channel_observed_poll(&r, 15, &o) == -1 && errno == ETIMEDOUT);
    CHECK(timer_ms_gettime64() - started < 70); pthread_join(t, NULL);
    t = start_holder(&holder, &queue_proc_mutex, 80);
    CHECK(snd_channel_observed_consume(&r, 15) == -1 && errno == ETIMEDOUT &&
          !slots[2].ack_pending && slots[2].token == r.token);
    pthread_join(t, NULL);
    t = start_holder(&holder, &channel_mutex[2], 80);
    CHECK(snd_channel_observed_consume(&r, 15) == -1 && errno == ETIMEDOUT); pthread_join(t, NULL);
    CHECK(lock_until(&bind_mutex, timer_ms_gettime64()) == -1 && errno == ETIMEDOUT && !timed_zero);
    CHECK(snd_iface_observation_submit(NULL, 0, timer_ms_gettime64()) == -1 && errno == ETIMEDOUT);
    /* Publication success is admission even if the last copy crosses the budget. */
    publish_delay_ms = 110; CHECK(submit(4, &saved_r) == 0 && slots[4].token == saved_r.token); publish_delay_ms = 0;
    /* Sequence boundaries and session loss fail closed without consuming receipts. */
    results[2].sequence = UINT32_MAX;
    CHECK(snd_channel_observed_poll(&r, 50, &o) == -1 && errno == EOVERFLOW && slots[2].token == r.token);
    results[2].sequence = 1;
    CHECK(snd_channel_observed_poll(&r, 5, &o) == -1 && errno == ETIMEDOUT);
    results[2].sequence = 2; header.active_session = 0;
    CHECK(snd_channel_observed_poll(&r, 20, &o) == -1 && errno == ENODEV);
    header.active_session = session; CHECK(submit(5, &saved_r) == -1 && errno == ENODEV);
    reset(); driver_features = 0; CHECK(submit(0, &r) == -1 && errno == ENOTSUP && !packets);
    driver_features = SND_DRIVER_FEATURE_OBSERVATION; initted = false;
    CHECK(submit(0, &r) == -1 && errno == ENODEV); initted = true;
    in_irq = 1; CHECK(submit(0, &r) == -1 && errno == EPERM); in_irq = 0;
    /* Timed-out first BIND can be retried without resetting the session. */
    paused = true; CHECK(submit(0, &r) == -1 && errno == ETIMEDOUT && !slots[0].token);
    uint32_t same_session = session; paused = false;
    CHECK(submit(0, &r) == 0 && session == same_session);
    CHECK(snd_channel_observed_poll(&r, 50, &o) == 0);
    cookie = UINT32_MAX; CHECK(snd_channel_observed_consume(&r, 50) == -1 && errno == EOVERFLOW && slots[0].token == r.token);
    uint32_t prior_session = session; snd_observation_load_begin(); snd_observation_load_begin();
    CHECK(session == prior_session + 2); /* failed load attempts burn generations */
    session_counter = UINT32_MAX; snd_observation_load_begin();
    CHECK(submit(0, &r) == -1 && errno == EOVERFLOW);
    snd_observation_reset(); CHECK(submit(0, &r) == -1 && errno == ENODEV);
    CHECK(!timed_zero);
    printf("SH-4/ARM observer: %u checks passed\n", checks); return 0;
}
