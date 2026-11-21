/* Original single-controller command exchange over the PCM producer. */
#include "adx_service_internal.h"
#include "adx_container_internal.h"
#include "adx_joint_reader_internal.h"
#include "adx_stream_internal.h"
#include <kosdec/adx_loader.h>
#include <errno.h>
#include <string.h>
#define SERVICE_MAGIC UINT32_C(0x41445856)
enum { EMPTY, WRITING, PENDING, COMPLETE, CLOSED = 4 };
enum { PAUSED, RUNNING, TERMINAL_BASE };

#define PROGRESS_SAMPLES_COMPLETE UINT32_C(0x80000000)
#define PROGRESS_LOOP UINT32_C(0x40000000)
#define PROGRESS_FRAME_BLOCK UINT32_C(0x20000000)
#define PROGRESS_PACKED_BLOCK UINT32_C(0x10000000)
#define PROGRESS_FLAGS (PROGRESS_LOOP | PROGRESS_FRAME_BLOCK | PROGRESS_PACKED_BLOCK | \
                        PROGRESS_SAMPLES_COMPLETE)
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
struct container_progress {
    uint32_t rate, position, declared, bytes;
    unsigned block_frames;
};

static struct container_progress container_progress(const struct kosdec_adx_pipe *pipe) {
    if(pipe->wave && pipe->wave->header_ready && !pipe->wave->seeking) {
        const struct kosdec_adx_wave *w = pipe->wave;
        return (struct container_progress){w->info.sample_rate, w->frames_done,
            w->info.total_frames, w->used - w->input_pos, w->info.sample_bits == 4 ? 4u : 1u};
    }
    if(pipe->aiff && pipe->aiff->header_ready) {
        const struct kosdec_adx_aiff *a = pipe->aiff;
        return (struct container_progress){a->info.sample_rate, a->frames_done,
            a->info.total_frames, a->used - a->input_pos, 1};
    }
    if(pipe->au && pipe->au->header_ready) {
        const struct kosdec_adx_au *a = pipe->au;
        return (struct container_progress){a->info.sample_rate, a->frames_done,
            a->info.total_frames, a->used - a->input_pos, 1};
    }
    if(pipe->spsd && pipe->spsd->header_ready) {
        const struct kosdec_adx_spsd *a = pipe->spsd;
        return (struct container_progress){a->info.sample_rate, a->frames_done,
            a->info.total_frames, a->used - a->input_pos, 1};
    }
    return (struct container_progress){0};
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
    else {
        struct container_progress progress = container_progress(pipe);
        bytes = progress.bytes;
        ready = ready && progress.block_frames;
    }
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
        initial.input_capacity = MEDIA_ADX_READ_CHUNK;
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
    if(result == KOSDEC_ADX_MORE) {
        initial.input_capacity = sizeof(wave->buffer);
        initial.input_data = input_snapshot(&initial.pipe);
        *s = initial;
    }
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_au(struct kosdec_adx_service *s,
    struct kosdec_adx_au *au, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_au(&initial.pipe,
        au, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) {
        initial.input_capacity = sizeof(au->buffer);
        initial.input_data = input_snapshot(&initial.pipe);
        *s = initial;
    }
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_aiff(struct kosdec_adx_service *s,
    struct kosdec_adx_aiff *aiff, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_aiff(&initial.pipe,
        aiff, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) {
        initial.input_capacity = sizeof(aiff->buffer);
        initial.input_data = input_snapshot(&initial.pipe);
        *s = initial;
    }
    return result;
}
enum kosdec_adx_status kosdec_adx_service_init_spsd(struct kosdec_adx_service *s,
    struct kosdec_adx_spsd *spsd, int16_t *pcm, size_t samples, size_t capacity) {
    if(!s) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_service initial = {.magic = SERVICE_MAGIC};
    enum kosdec_adx_status result = kosdec_adx_pipe_init_spsd(&initial.pipe,
        spsd, pcm, samples, capacity);
    if(result == KOSDEC_ADX_MORE) {
        initial.input_capacity = sizeof(spsd->buffer);
        initial.input_data = input_snapshot(&initial.pipe);
        *s = initial;
    }
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
static void publish_progress(struct kosdec_adx_service *s, uint32_t data,
                             size_t emitted, uint32_t state) {
    const struct kosdec_adx_pipe *pipe = &s->pipe;
    const struct kosdec_adx_joint_reader *joint = pipe->joint;
    const struct kosdec_adx_sequence *sequence = pipe->sequence;
    const struct kosdec_adx_stream *stream = pipe->stream;
    const struct kosdec_adx_decoder *decoder = NULL;
    const bool done = state == TERMINAL_BASE + KOSDEC_ADX_DONE;
    bool final_record = false;
    uint32_t position = 0;
    if(joint) {
        position = joint->decoder.frames_done;
        if(joint->decoder.header_ready && !joint->linked.scan_active) {
            decoder = &joint->decoder;
            final_record = (!joint->sfa_live || joint->sfa_length_known) &&
                           !joint->job.trap && !joint->linked.enabled;
        }
    } else {
        if(sequence && (sequence->active || (done && sequence->completed_entries)))
            stream = &sequence->reader;
        if(stream) {
            position = stream->source_frame;
            if(stream->prepared && !stream->seeking && (!sequence || sequence->admitted || done)) {
                decoder = &stream->decoder;
                final_record = !stream->repeat && !stream->linked && !sequence;
            }
        }
    }

    uint32_t rate = 0, declared = 0;
    if(decoder) {
        rate = decoder->sample_rate;
        declared = decoder->total_frames;
        if(decoder->loop.present)
            state |= PROGRESS_LOOP;
    } else if(media_adx_pipe_has_container(pipe)) {
        struct container_progress progress = container_progress(pipe);
        rate = progress.rate;
        position = progress.position;
        declared = progress.declared;
        final_record = progress.block_frames != 0;
        if(progress.block_frames == 4)
            state |= PROGRESS_PACKED_BLOCK;
        else if(progress.block_frames == 1)
            state |= PROGRESS_FRAME_BLOCK;
    }
    if(done || (final_record && position == declared))
        state |= PROGRESS_SAMPLES_COMPLETE;
    uint32_t frames = __atomic_load_n(&s->progress_frames,__ATOMIC_RELAXED) + (uint32_t)emitted;
    uint32_t epoch = __atomic_load_n(&s->progress_epoch,__ATOMIC_RELAXED);

    /* Only the opted-in producer writes. All snapshot words and both epoch
       markers share the sequentially consistent order. A preempted writer
       cannot make a higher-priority reader spin. */
    __atomic_store_n(&s->progress_epoch,epoch + 1,__ATOMIC_SEQ_CST);
    __atomic_store_n(&s->input_data,data,__ATOMIC_SEQ_CST);
    __atomic_store_n(&s->progress_frames,frames,__ATOMIC_SEQ_CST);
    __atomic_store_n(&s->progress_position,position,__ATOMIC_SEQ_CST);
    __atomic_store_n(&s->progress_format,rate,__ATOMIC_SEQ_CST);
    __atomic_store_n(&s->progress_declared_frames,declared,__ATOMIC_SEQ_CST);
    __atomic_store_n(&s->progress_state,state,__ATOMIC_SEQ_CST);
    __atomic_store_n(&s->progress_epoch,epoch + 2,__ATOMIC_SEQ_CST);
}

int kosdec_adx_service_enable_decode_progress(struct kosdec_adx_service *s) {
    if(!valid(s) || !kosdec_adx_service_get_state(s).paused) {
        errno = EINVAL;
        return -1;
    }
    if(!s->input_capacity) {
        errno = ENOTSUP;
        return -1;
    }
    if(!s->progress_enabled) {
        publish_progress(s,input_snapshot(&s->pipe),0,PAUSED);
        s->progress_enabled = true;
    }
    return 0;
}

int kosdec_adx_service_get_decode_progress(const struct kosdec_adx_service *s,
                                          struct kosdec_adx_decode_progress *out) {
    if(!valid(s) || !out) {
        errno = EINVAL;
        return -1;
    }
    if(!s->progress_enabled) {
        errno = ENOTSUP;
        return -1;
    }
    uint32_t epoch = __atomic_load_n(&s->progress_epoch,__ATOMIC_SEQ_CST);
    if(epoch & 1u) {
        errno = EAGAIN;
        return -1;
    }
    uint32_t frames = __atomic_load_n(&s->progress_frames,__ATOMIC_SEQ_CST);
    uint32_t position = __atomic_load_n(&s->progress_position,__ATOMIC_SEQ_CST);
    uint32_t rate = __atomic_load_n(&s->progress_format,__ATOMIC_SEQ_CST);
    uint32_t declared = __atomic_load_n(&s->progress_declared_frames,__ATOMIC_SEQ_CST);
    uint32_t state = __atomic_load_n(&s->progress_state,__ATOMIC_SEQ_CST);
    uint32_t data = __atomic_load_n(&s->input_data,__ATOMIC_SEQ_CST);
    if(__atomic_load_n(&s->progress_epoch,__ATOMIC_SEQ_CST) != epoch) {
        errno = EAGAIN;
        return -1;
    }
    uint32_t status = state & ~PROGRESS_FLAGS;
    *out = (struct kosdec_adx_decode_progress){
        .decoded_frames = frames, .source_frame = position,
        .input_bytes = data & ~MEDIA_ADX_INPUT_FORMAT_READY,
        .sample_rate = rate,
        .status = status < TERMINAL_BASE ? KOSDEC_ADX_MORE :
                  (enum kosdec_adx_status)(status - TERMINAL_BASE),
        .loop_present = (state & PROGRESS_LOOP) != 0, .paused = status == PAUSED,
        .samples_complete = (state & PROGRESS_SAMPLES_COMPLETE) != 0,
        .declared_frames = declared,
        .block_frames = !rate ? 0 : state & PROGRESS_PACKED_BLOCK ? 4 :
                        state & PROGRESS_FRAME_BLOCK ? 1 : 32
    };
    return 0;
}

static void publish_controls(struct kosdec_adx_service *s, struct kosdec_adx_stream_result result) {
    if(s->input_capacity) {
        uint32_t data = input_snapshot(&s->pipe);
        if(s->progress_enabled) {
            uint32_t state = result.status >= KOSDEC_ADX_DONE ? TERMINAL_BASE + result.status :
                             __atomic_load_n(&s->state,__ATOMIC_RELAXED);
            publish_progress(s,data,result.frames,state);
        } else {
            __atomic_store_n(&s->input_data,data,__ATOMIC_RELEASE);
        }
    }
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
static void publish_step(struct kosdec_adx_service *s, struct kosdec_adx_stream_result r) {
    publish_controls(s,r);
    if(r.status >= KOSDEC_ADX_DONE) {
        __atomic_store_n(&s->state, TERMINAL_BASE + r.status, __ATOMIC_RELEASE);
        __atomic_fetch_or(&s->mailbox, CLOSED, __ATOMIC_RELEASE);
    }
}

enum kosdec_adx_status media_adx_service_step_transport(struct kosdec_adx_service *s, bool reload) {
    struct kosdec_adx_joint_reader *joint = s->pipe.joint;
    struct kosdec_adx_stream_result r = {0};
    if(s->pipe.stream)
        r.status = media_adx_stream_step_file(s->pipe.stream);
    else if(s->pipe.sequence)
        r.status = media_adx_sequence_step_file(s->pipe.sequence);
    else if(s->pipe.wave)
        r.status = media_adx_wave_step_file(s->pipe.wave);
    else if(s->pipe.aiff)
        r.status = media_adx_aiff_step_file(s->pipe.aiff);
    else if(s->pipe.au)
        r.status = media_adx_au_step_file(s->pipe.au);
    else if(s->pipe.spsd)
        r.status = media_adx_spsd_step_file(s->pipe.spsd);
    else if(joint->sector.request.read_at)
        r.status = media_adx_joint_sector_step_stream(joint);
    else
        r.status = media_adx_joint_reader_step_loader(joint,reload);
    if(r.status >= KOSDEC_ADX_DONE)
        __atomic_store_n(&s->pipe.terminal,r.status,__ATOMIC_RELEASE);
    publish_step(s,r);
    return r.status;
}

struct kosdec_adx_stream_result media_adx_service_step_limited(
    struct kosdec_adx_service *s, size_t frame_limit, bool await_format_admission, bool separate_transport) {
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
        publish_controls(s,r);
        __atomic_store_n(&s->mailbox, COMPLETE, __ATOMIC_RELEASE);
        return r;
    }
    else if(state.paused) return r;
    else if(await_format_admission && __atomic_load_n(&s->progress_format,__ATOMIC_RELAXED)) {
        r.status = KOSDEC_ADX_NEED_OUTPUT;
        return r;
    }
    else r = media_adx_pipe_pump_limited(&s->pipe,frame_limit,separate_transport);
    publish_step(s,r);
    return r;
}
struct kosdec_adx_stream_result kosdec_adx_service_step(struct kosdec_adx_service *s) {
    return media_adx_service_step_limited(s,0,false,false);
}
