/* Original bounded descriptor queue and independent-file PCM transitions. */
#include <kosdec/adx_sequence.h>
#include <string.h>
#define SEQUENCE_MAGIC UINT32_C(0x41445851)
static bool valid(const struct kosdec_adx_sequence *s) {
    return s && s->magic == SEQUENCE_MAGIC;
}
void kosdec_adx_sequence_init(struct kosdec_adx_sequence *s, bool repeat) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    s->magic = SEQUENCE_MAGIC;
    s->repeat = repeat;
}
enum kosdec_adx_status kosdec_adx_sequence_enqueue(
    struct kosdec_adx_sequence *s, const struct kosdec_adx_source *source) {
    if(!valid(s) || !source || !source->read_at || !source->bytes)
        return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->terminal) return s->terminal;
    if(s->count == KOSDEC_ADX_SEQUENCE_CAPACITY) return KOSDEC_ADX_NEED_OUTPUT;
    s->queue[(s->head + s->count) % KOSDEC_ADX_SEQUENCE_CAPACITY] = *source;
    ++s->count;
    return KOSDEC_ADX_MORE;
}
enum kosdec_adx_status kosdec_adx_sequence_start(struct kosdec_adx_sequence *s) {
    if(!valid(s)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->terminal) return s->terminal;
    if(s->started) return KOSDEC_ADX_BAD_ARGUMENT;
    if(!s->count) return KOSDEC_ADX_NEED_INPUT;
    s->started = true;
    return KOSDEC_ADX_MORE;
}
void kosdec_adx_sequence_pause(struct kosdec_adx_sequence *s, bool paused) {
    if(valid(s) && !s->terminal) s->paused = paused;
}
void kosdec_adx_sequence_repeat(struct kosdec_adx_sequence *s, bool repeat) {
    if(valid(s) && !s->terminal) s->repeat = repeat;
}
void kosdec_adx_sequence_cancel(struct kosdec_adx_sequence *s) {
    if(valid(s) && !s->terminal) {
        if(s->active) kosdec_adx_stream_cancel(&s->reader);
        s->terminal = KOSDEC_ADX_CANCELLED;
    }
}
struct kosdec_adx_sequence_result kosdec_adx_sequence_read(
    struct kosdec_adx_sequence *s, int16_t *output, size_t capacity) {
    struct kosdec_adx_sequence_result r = {.status = KOSDEC_ADX_BAD_ARGUMENT};
    if(!valid(s) || (!output && capacity) ||
       (output && (uintptr_t)output % _Alignof(int16_t))) return r;
    if(s->count) r.tag = s->queue[s->head].tag;
    if(s->active) r.source_frame = s->reader.source_frame;
    if(s->terminal) { r.status = s->terminal; return r; }
    if(!s->started) return r;
    r.status = KOSDEC_ADX_MORE;
    if(s->paused) return r;
    if(!capacity) { r.status = KOSDEC_ADX_NEED_OUTPUT; return r; }
    if(!s->active) {
        const struct kosdec_adx_source *source = &s->queue[s->head];
        kosdec_adx_stream_init(&s->reader, source->read_at, source->context,
                              source->bytes, false);
        s->active = true;
    }
    struct kosdec_adx_stream_result step = kosdec_adx_stream_read(
        &s->reader, output, capacity);
    r.status = step.status;
    r.frames = step.frames;
    if(!s->admitted && s->reader.prepared && r.status < KOSDEC_ADX_DONE) {
        const struct kosdec_adx_decoder *d = &s->reader.decoder;
        /* stream's header step returns no PCM; validate before its next step. */
        if(d->loop.present || (s->format_ready &&
           (s->sample_rate != d->sample_rate || s->channels != d->channels))) {
            r.status = KOSDEC_ADX_UNSUPPORTED;
        }
        else {
            s->sample_rate = d->sample_rate;
            s->channels = d->channels;
            s->format_ready = s->admitted = true;
            if(r.status == KOSDEC_ADX_NEED_OUTPUT) r.status = KOSDEC_ADX_MORE;
        }
    }
    s->returned_frames += r.frames;
    if(r.status == KOSDEC_ADX_DONE) {
        struct kosdec_adx_source completed = s->queue[s->head];
        s->head = (s->head + 1) % KOSDEC_ADX_SEQUENCE_CAPACITY;
        --s->count;
        ++s->completed_entries;
        if(s->repeat) {
            s->queue[(s->head + s->count) % KOSDEC_ADX_SEQUENCE_CAPACITY] = completed;
            ++s->count;
            ++s->requeues;
        }
        s->active = s->admitted = false;
        if(s->count) r.status = KOSDEC_ADX_MORE;
    }
    if(r.status >= KOSDEC_ADX_DONE) s->terminal = r.status;
    return r;
}
