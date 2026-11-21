/* Original single-controller command exchange over the PCM producer. */
#include "adx_service_internal.h"
#include <kosdec/adx_loader.h>
#include <string.h>
#define SERVICE_MAGIC UINT32_C(0x41445856)
enum { EMPTY, WRITING, PENDING, COMPLETE, CLOSED = 4 };
enum { PAUSED, RUNNING, TERMINAL_BASE };
_Static_assert(__GCC_ATOMIC_INT_LOCK_FREE == 2, "lock-free controls required");
static bool valid(const struct kosdec_adx_service *s) {
    return s && s->magic == SERVICE_MAGIC;
}
static uint32_t queued_sources(const struct kosdec_adx_pipe *pipe) {
    if(pipe->sequence) return pipe->sequence->count;
    struct kosdec_adx_loader_info info;
    return pipe->joint && kosdec_adx_loader_get_info(pipe->joint->linked.loader, &info) ?
        info.pending : 0;
}
static bool follow_links(const struct kosdec_adx_pipe *pipe) {
    if(pipe->stream) return pipe->stream->linked && pipe->stream->follow_links;
    return pipe->joint && pipe->joint->linked.enabled && pipe->joint->linked.follow;
}
static const struct kosdec_adx_joint *input_joint(const struct kosdec_adx_pipe *pipe) {
    return pipe->joint ? pipe->joint->joint : NULL;
}
static uint32_t input_snapshot(const struct kosdec_adx_pipe *pipe) {
    const struct kosdec_adx_joint *joint = input_joint(pipe);
    const struct kosdec_adx_stream *stream = pipe->stream;
    bool ready = __atomic_load_n(&pipe->ready, __ATOMIC_RELAXED);
    if(pipe->sequence && pipe->sequence->active)
        stream = &pipe->sequence->reader;
    uint32_t bytes;
    if(joint)
        bytes = (uint32_t)joint->available[KOSDEC_ADX_JOINT_DATA];
    else if(stream) {
        bytes = stream->input_count - stream->input_used;
        ready = ready && stream->prepared && !stream->seeking &&
                (!pipe->sequence || pipe->sequence->admitted);
    }
    else
        return 0;
    return bytes | (ready ? MEDIA_ADX_INPUT_FORMAT_READY : 0);
}
enum kosdec_adx_status kosdec_adx_service_init(struct kosdec_adx_service *s,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init(&initial.pipe,
        stream, sequence, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) {
        initial.queued_sources=sequence?sequence->count:0;
        initial.link_enabled=stream && stream->linked && stream->follow_links;
        initial.input_capacity = sizeof(stream->input);
        initial.input_data = input_snapshot(&initial.pipe);
        *s = initial;
    }
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_joint(struct kosdec_adx_service *s,
    struct kosdec_adx_joint_reader *joint, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_joint(&initial.pipe,
        joint, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) {
        initial.queued_sources = queued_sources(&initial.pipe);
        initial.link_enabled = follow_links(&initial.pipe);
        const struct kosdec_adx_joint *input = input_joint(&initial.pipe);
        if(input) {
            initial.input_capacity = (uint32_t)input->capacity;
            initial.input_data = input_snapshot(&initial.pipe);
        }
        *s = initial;
    }
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
bool kosdec_adx_service_get_queue_count(const struct kosdec_adx_service *s,uint32_t *count) {
    if(!valid(s) || !count) return false;
    *count=__atomic_load_n(&s->queued_sources,__ATOMIC_ACQUIRE);
    return true;
}
bool kosdec_adx_service_get_linked(const struct kosdec_adx_service *s,bool *enabled) {
    if(!valid(s) || !enabled || (!s->pipe.stream &&
       !(s->pipe.joint && s->pipe.joint->linked.enabled))) return false;
    *enabled=__atomic_load_n(&s->link_enabled,__ATOMIC_ACQUIRE)!=0;
    return true;
}
static void publish_controls(struct kosdec_adx_service *s) {
    if(s->input_capacity)
        __atomic_store_n(&s->input_data,input_snapshot(&s->pipe),__ATOMIC_RELEASE);
    __atomic_store_n(&s->queued_sources,queued_sources(&s->pipe),__ATOMIC_RELEASE);
    __atomic_store_n(&s->link_enabled,follow_links(&s->pipe),__ATOMIC_RELEASE);
}
enum kosdec_adx_status kosdec_adx_service_post(struct kosdec_adx_service *s,
    const struct kosdec_adx_command *c) {
    if(!valid(s) || !c || c->kind < KOSDEC_ADX_PRODUCTION_PAUSE ||
       c->kind > KOSDEC_ADX_SET_LINK_FOLLOW) return KOSDEC_ADX_BAD_ARGUMENT;
    if((c->kind == KOSDEC_ADX_ENQUEUE || c->kind == KOSDEC_ADX_LOADER_ENQUEUE) &&
       (!c->source.read_at || !c->source.bytes)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(c->kind == KOSDEC_ADX_LOADER_ENQUEUE && !memchr(c->label, 0, sizeof(c->label)))
        return KOSDEC_ADX_BAD_ARGUMENT;
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
bool kosdec_adx_service_take_receipt(struct kosdec_adx_service *s,
                                    struct kosdec_adx_command_receipt *result) {
    if(!valid(s) || !result) return false;
    uint32_t mailbox = __atomic_load_n(&s->mailbox, __ATOMIC_ACQUIRE);
    if((mailbox & ~CLOSED) == COMPLETE)
        *result = (struct kosdec_adx_command_receipt){s->reply, s->reply_source_id};
    else if(mailbox == (CLOSED | PENDING))
        *result = (struct kosdec_adx_command_receipt){kosdec_adx_service_get_state(s).status, 0};
    else return false;
    /* A producer may close a COMPLETE slot while its receipt is being read. */
    __atomic_fetch_and(&s->mailbox, CLOSED, __ATOMIC_RELEASE);
    return true;
}
bool kosdec_adx_service_take(struct kosdec_adx_service *s,
                            enum kosdec_adx_status *result) {
    struct kosdec_adx_command_receipt receipt;
    if(!result || !kosdec_adx_service_take_receipt(s, &receipt)) return false;
    *result = receipt.status;
    return true;
}
void kosdec_adx_service_request_cancel(struct kosdec_adx_service *s) {
    if(valid(s)) __atomic_store_n(&s->cancel_requested, 1, __ATOMIC_RELEASE);
}
static enum kosdec_adx_status apply(struct kosdec_adx_service *s) {
    const struct kosdec_adx_command *c = &s->command;
    struct kosdec_adx_joint_reader *joint = s->pipe.joint;
    struct kosdec_adx_loader *loader = joint ? joint->linked.loader : NULL;
    s->reply_source_id = 0;
    switch(c->kind) {
    case KOSDEC_ADX_PRODUCTION_PAUSE:
        __atomic_store_n(&s->state, c->enabled ? PAUSED : RUNNING, __ATOMIC_RELEASE);
        return KOSDEC_ADX_MORE;
    case KOSDEC_ADX_SEQUENCE_REPEAT:
        if(loader) {
            if(joint->linked.registration_closed && c->enabled) return KOSDEC_ADX_BAD_ARGUMENT;
            return kosdec_adx_loader_set_repeat(loader, c->enabled) ==
                KOSDEC_ADX_LOADER_OK ? KOSDEC_ADX_MORE : KOSDEC_ADX_IO_ERROR;
        }
        if(!s->pipe.sequence) return KOSDEC_ADX_UNSUPPORTED;
        kosdec_adx_sequence_repeat(s->pipe.sequence, c->enabled);
        return KOSDEC_ADX_MORE;
    case KOSDEC_ADX_RELEASE_LOOP:
        if(s->pipe.joint) return kosdec_adx_joint_reader_release_loop(s->pipe.joint);
        if(!s->pipe.stream) return KOSDEC_ADX_UNSUPPORTED;
        kosdec_adx_stream_release_loop(s->pipe.stream);
        return KOSDEC_ADX_MORE;
    case KOSDEC_ADX_ENQUEUE:
        if(!s->pipe.sequence) return KOSDEC_ADX_UNSUPPORTED;
        return kosdec_adx_sequence_enqueue(s->pipe.sequence, &c->source);
    case KOSDEC_ADX_RELEASE_LINK:
        if(s->pipe.joint) return kosdec_adx_joint_reader_release_link(s->pipe.joint);
        if(!s->pipe.stream) return KOSDEC_ADX_UNSUPPORTED;
        return kosdec_adx_stream_release_link(s->pipe.stream);
    case KOSDEC_ADX_SET_LINK_FOLLOW:
        if(joint) return kosdec_adx_joint_reader_set_link_follow(joint, c->enabled);
        if(!s->pipe.stream) return KOSDEC_ADX_UNSUPPORTED;
        return kosdec_adx_stream_set_link_follow(s->pipe.stream, c->enabled);
    case KOSDEC_ADX_LOADER_ENQUEUE: {
        if(!loader) return KOSDEC_ADX_UNSUPPORTED;
        if(joint->linked.seal_when_empty) return KOSDEC_ADX_BAD_ARGUMENT;
        enum kosdec_adx_loader_status status = kosdec_adx_loader_enqueue(loader, c->label,
            c->source.read_at, c->source.context, c->source.bytes, 0, UINT32_MAX,
            &s->reply_source_id);
        if(status == KOSDEC_ADX_LOADER_OK) return KOSDEC_ADX_MORE;
        if(status == KOSDEC_ADX_LOADER_LIMIT) return KOSDEC_ADX_NEED_OUTPUT;
        if(status == KOSDEC_ADX_LOADER_BAD_ARGUMENT) return KOSDEC_ADX_BAD_ARGUMENT;
        return KOSDEC_ADX_IO_ERROR;
    }
    case KOSDEC_ADX_LOADER_FINISH:
        if(!loader) return KOSDEC_ADX_UNSUPPORTED;
        if(kosdec_adx_loader_set_repeat(loader, false) != KOSDEC_ADX_LOADER_OK)
            return KOSDEC_ADX_IO_ERROR;
        joint->linked.registration_closed = true;
        joint->linked.seal_when_empty = true;
        return KOSDEC_ADX_MORE;
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
        publish_controls(s);
        __atomic_store_n(&s->mailbox, COMPLETE, __ATOMIC_RELEASE);
        return r;
    }
    else if(state.paused) return r;
    else r = kosdec_adx_pipe_pump(&s->pipe);
    publish_controls(s);
    if(r.status >= KOSDEC_ADX_DONE) {
        __atomic_store_n(&s->state, TERMINAL_BASE + r.status, __ATOMIC_RELEASE);
        __atomic_fetch_or(&s->mailbox, CLOSED, __ATOMIC_RELEASE);
    }
    return r;
}
