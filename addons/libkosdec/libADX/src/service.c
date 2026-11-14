/* Original single-controller command exchange over the PCM producer. */
#include <kosdec/adx_service.h>
#define SERVICE_MAGIC UINT32_C(0x41445856)
enum { EMPTY, WRITING, PENDING, COMPLETE, CLOSED = 4 };
enum { PAUSED, RUNNING, TERMINAL_BASE };
_Static_assert(__GCC_ATOMIC_INT_LOCK_FREE == 2, "lock-free controls required");
static bool valid(const struct kosdec_adx_service *s) {
    return s && s->magic == SERVICE_MAGIC;
}
enum kosdec_adx_status kosdec_adx_service_init(struct kosdec_adx_service *s,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init(&initial.pipe,
        stream, sequence, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) *s = initial;
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_joint(struct kosdec_adx_service *s,
    struct kosdec_adx_joint_reader *joint, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_joint(&initial.pipe,
        joint, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) *s = initial;
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_wave(struct kosdec_adx_service *s,
    struct kosdec_adx_wave *wave, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_wave(&initial.pipe,
        wave, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) *s = initial;
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_au(struct kosdec_adx_service *s,
    struct kosdec_adx_au *au, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_au(&initial.pipe,
        au, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) *s = initial;
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_aiff(struct kosdec_adx_service *s,
    struct kosdec_adx_aiff *aiff, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_aiff(&initial.pipe,
        aiff, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) *s = initial;
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_spsd(struct kosdec_adx_service *s,
    struct kosdec_adx_spsd *spsd, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_spsd(&initial.pipe,
        spsd, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) *s = initial;
    return result;
}
struct kosdec_adx_service_state kosdec_adx_service_get_state(
    const struct kosdec_adx_service *s) {
    struct kosdec_adx_service_state r = {KOSDEC_ADX_BAD_ARGUMENT, false};
    if(!valid(s)) return r;
    uint32_t state = __atomic_load_n(&s->state, __ATOMIC_ACQUIRE);
    r.status = state < TERMINAL_BASE ? KOSDEC_ADX_MORE :
        (enum kosdec_adx_status)(state - TERMINAL_BASE);
    r.paused = state == PAUSED;
    return r;
}
enum kosdec_adx_status kosdec_adx_service_post(struct kosdec_adx_service *s,
    const struct kosdec_adx_command *c) {
    if(!valid(s) || !c || c->kind < KOSDEC_ADX_PRODUCTION_PAUSE ||
       c->kind > KOSDEC_ADX_ENQUEUE || (c->kind == KOSDEC_ADX_ENQUEUE &&
       (!c->source.read_at || !c->source.bytes))) return KOSDEC_ADX_BAD_ARGUMENT;
    uint32_t expected = EMPTY;
    if(!__atomic_compare_exchange_n(&s->mailbox, &expected, WRITING, false,
                                   __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
        return expected & CLOSED ? kosdec_adx_service_get_state(s).status :
                                   KOSDEC_ADX_NEED_OUTPUT;
    s->command = *c;
    /* Preserve a concurrent terminal closure while publishing the payload.
       A closed pending command is collected as rejected by the controller. */
    __atomic_fetch_add(&s->mailbox, PENDING - WRITING, __ATOMIC_RELEASE);
    return KOSDEC_ADX_MORE;
}
bool kosdec_adx_service_take(struct kosdec_adx_service *s,
                           enum kosdec_adx_status *result) {
    if(!valid(s) || !result) return false;
    uint32_t mailbox = __atomic_load_n(&s->mailbox, __ATOMIC_ACQUIRE);
    if((mailbox & ~CLOSED) == COMPLETE) *result = s->reply;
    else if(mailbox == (CLOSED | PENDING))
        *result = kosdec_adx_service_get_state(s).status;
    else return false;
    /* A producer may close a COMPLETE slot while its receipt is being read. */
    __atomic_fetch_and(&s->mailbox, CLOSED, __ATOMIC_RELEASE);
    return true;
}
void kosdec_adx_service_request_cancel(struct kosdec_adx_service *s) {
    if(valid(s)) __atomic_store_n(&s->cancel_requested, 1, __ATOMIC_RELEASE);
}
static enum kosdec_adx_status apply(struct kosdec_adx_service *s) {
    const struct kosdec_adx_command *c = &s->command;
    switch(c->kind) {
    case KOSDEC_ADX_PRODUCTION_PAUSE:
        __atomic_store_n(&s->state, c->enabled ? PAUSED : RUNNING, __ATOMIC_RELEASE);
        return KOSDEC_ADX_MORE;
    case KOSDEC_ADX_SEQUENCE_REPEAT:
        if(!s->pipe.sequence) return KOSDEC_ADX_UNSUPPORTED;
        kosdec_adx_sequence_repeat(s->pipe.sequence, c->enabled);
        return KOSDEC_ADX_MORE;
    case KOSDEC_ADX_RELEASE_LOOP:
        if(!s->pipe.stream) return KOSDEC_ADX_UNSUPPORTED;
        kosdec_adx_stream_release_loop(s->pipe.stream);
        return KOSDEC_ADX_MORE;
    case KOSDEC_ADX_ENQUEUE:
        if(!s->pipe.sequence) return KOSDEC_ADX_UNSUPPORTED;
        return kosdec_adx_sequence_enqueue(s->pipe.sequence, &c->source);
    }
    return KOSDEC_ADX_BAD_ARGUMENT;
}
struct kosdec_adx_stream_result kosdec_adx_service_step(struct kosdec_adx_service *s) {
    struct kosdec_adx_stream_result r = {KOSDEC_ADX_BAD_ARGUMENT, 0};
    if(!valid(s)) return r;
    struct kosdec_adx_service_state state = kosdec_adx_service_get_state(s);
    r.status = state.status;
    if(r.status >= KOSDEC_ADX_DONE) return r;
    if(__atomic_load_n(&s->cancel_requested, __ATOMIC_ACQUIRE)) {
        kosdec_adx_pipe_request_cancel(&s->pipe);
        r = kosdec_adx_pipe_pump(&s->pipe);
    }
    else if(__atomic_load_n(&s->mailbox, __ATOMIC_ACQUIRE) == PENDING) {
        s->reply = apply(s);
        __atomic_store_n(&s->mailbox, COMPLETE, __ATOMIC_RELEASE);
        return r;
    }
    else if(state.paused) return r;
    else r = kosdec_adx_pipe_pump(&s->pipe);
    if(r.status >= KOSDEC_ADX_DONE) {
        __atomic_store_n(&s->state, TERMINAL_BASE + r.status, __ATOMIC_RELEASE);
        __atomic_fetch_or(&s->mailbox, CLOSED, __ATOMIC_RELEASE);
    }
    return r;
}
