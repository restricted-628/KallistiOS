/* Original stream/sequence PCM bridge. SPSC ownership/order follows KOS's
   snd_adx_pipe.c, Copyright(C)2026 Joseph Black, under the KOS license. */
#include <kosdec/adx_pipe.h>
#include <string.h>
#define PIPE_MAGIC UINT32_C(0x41445850)
_Static_assert(sizeof(uint32_t) == sizeof(unsigned int), "32-bit atomic cursors");
_Static_assert(__GCC_ATOMIC_INT_LOCK_FREE == 2, "lock-free atomic cursors required");
static bool valid(const struct kosdec_adx_pipe *p) {
    return p && p->magic == PIPE_MAGIC;
}
static enum kosdec_adx_status init(struct kosdec_adx_pipe *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    struct kosdec_adx_joint_reader *joint, struct kosdec_adx_wave *wave, struct kosdec_adx_au *au, struct kosdec_adx_aiff *aiff, struct kosdec_adx_spsd *spsd,
    int16_t *pcm, size_t samples, size_t capacity) {
    if(!p || (!!stream + !!sequence + !!joint + !!wave + !!au + !!aiff + !!spsd != 1) || !pcm ||
       (uintptr_t)pcm % _Alignof(int16_t) || capacity < 32 ||
       capacity > 1048576 || (capacity & (capacity - 1)) || samples < 2 * capacity)
        return KOSDEC_ADX_BAD_ARGUMENT;
    memset(p, 0, sizeof(*p));
    p->magic = PIPE_MAGIC;
    p->pcm = pcm;
    p->capacity = (uint32_t)capacity;
    p->stream = stream;
    p->sequence = sequence;
    p->joint = joint;
    p->wave = wave;
    p->au = au;
    p->aiff = aiff;
    p->spsd = spsd;
    return KOSDEC_ADX_MORE;
}
enum kosdec_adx_status kosdec_adx_pipe_init(struct kosdec_adx_pipe *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    int16_t *pcm, size_t samples, size_t capacity) {
    return init(p, stream, sequence, NULL, NULL, NULL, NULL, NULL, pcm, samples, capacity);
}
enum kosdec_adx_status kosdec_adx_pipe_init_joint(struct kosdec_adx_pipe *p,
    struct kosdec_adx_joint_reader *joint, int16_t *pcm, size_t samples, size_t capacity) {
    return init(p, NULL, NULL, joint, NULL, NULL, NULL, NULL, pcm, samples, capacity);
}
enum kosdec_adx_status kosdec_adx_pipe_init_wave(struct kosdec_adx_pipe *p,
    struct kosdec_adx_wave *wave, int16_t *pcm, size_t samples, size_t capacity) {
    return init(p, NULL, NULL, NULL, wave, NULL, NULL, NULL, pcm, samples, capacity);
}
enum kosdec_adx_status kosdec_adx_pipe_init_au(struct kosdec_adx_pipe *p,
    struct kosdec_adx_au *au, int16_t *pcm, size_t samples, size_t capacity) {
    return init(p, NULL, NULL, NULL, NULL, au, NULL, NULL, pcm, samples, capacity);
}
enum kosdec_adx_status kosdec_adx_pipe_init_aiff(struct kosdec_adx_pipe *p,
    struct kosdec_adx_aiff *aiff, int16_t *pcm, size_t samples, size_t capacity) {
    return init(p, NULL, NULL, NULL, NULL, NULL, aiff, NULL, pcm, samples, capacity);
}
enum kosdec_adx_status kosdec_adx_pipe_init_spsd(struct kosdec_adx_pipe *p,
    struct kosdec_adx_spsd *spsd, int16_t *pcm, size_t samples, size_t capacity) {
    return init(p, NULL, NULL, NULL, NULL, NULL, NULL, spsd, pcm, samples, capacity);
}
bool kosdec_adx_pipe_get_format(const struct kosdec_adx_pipe *p,
                              uint32_t *rate, unsigned *channels) {
    if(!valid(p) || !rate || !channels ||
       !__atomic_load_n(&p->ready, __ATOMIC_ACQUIRE)) return false;
    *rate = p->sample_rate;
    *channels = p->channels;
    return true;
}
void kosdec_adx_pipe_request_cancel(struct kosdec_adx_pipe *p) {
    if(valid(p)) __atomic_store_n(&p->cancel_requested, 1, __ATOMIC_RELEASE);
}
bool kosdec_adx_pipe_get_loop_count(const struct kosdec_adx_pipe *p, uint32_t *loops) {
    if(!valid(p) || !loops) return false;
    *loops = __atomic_load_n(&p->decoded_loops, __ATOMIC_ACQUIRE);
    return true;
}
struct kosdec_adx_stream_result kosdec_adx_pipe_pump(struct kosdec_adx_pipe *p) {
    struct kosdec_adx_stream_result r = {KOSDEC_ADX_BAD_ARGUMENT, 0};
    if(!valid(p)) return r;
    uint32_t terminal = __atomic_load_n(&p->terminal, __ATOMIC_RELAXED);
    if(terminal) { r.status = (enum kosdec_adx_status)terminal; return r; }
    if(__atomic_load_n(&p->cancel_requested, __ATOMIC_ACQUIRE)) {
        if(p->stream) kosdec_adx_stream_cancel(p->stream);
        else if(p->joint) kosdec_adx_joint_reader_cancel(p->joint);
        else if(p->wave) kosdec_adx_wave_cancel(p->wave);
        else if(p->au) kosdec_adx_au_cancel(p->au);
        else if(p->aiff) kosdec_adx_aiff_cancel(p->aiff);
        else if(p->spsd) kosdec_adx_spsd_cancel(p->spsd);
        else kosdec_adx_sequence_cancel(p->sequence);
        r.status = KOSDEC_ADX_CANCELLED;
    }
    else {
        uint32_t head = __atomic_load_n(&p->write_cursor, __ATOMIC_RELAXED);
        uint32_t tail = __atomic_load_n(&p->read_cursor, __ATOMIC_ACQUIRE);
        uint32_t room = p->capacity - (uint32_t)(head - tail);
        if(!room) {
            r.status = KOSDEC_ADX_NEED_OUTPUT;
            return r;
        }
        if(room > 32) room = 32;
        int16_t pcm[64];
        if(p->stream && p->stream->seeking) {
            r.status = kosdec_adx_stream_seek_step(p->stream);
            if(r.status == KOSDEC_ADX_DONE && !p->stream->seeking)
                r.status = KOSDEC_ADX_MORE; /* Positioned; no PCM emitted yet. */
        }
        else if(p->stream) r = kosdec_adx_stream_read(p->stream, pcm, room);
        else if(p->joint) {
            struct kosdec_adx_joint_read_result s = kosdec_adx_joint_reader_read(p->joint, pcm, room);
            r.status = s.status; r.frames = s.frames;
        }
        else if(p->wave) {
            struct kosdec_adx_wave_result s = kosdec_adx_wave_read(p->wave, pcm, room);
            r.status = s.status; r.frames = s.frames;
        }
        else if(p->au) {
            struct kosdec_adx_au_result s = kosdec_adx_au_read(p->au, pcm, room);
            r.status = s.status; r.frames = s.frames;
        }
        else if(p->aiff) {
            struct kosdec_adx_aiff_result s = kosdec_adx_aiff_read(p->aiff, pcm, room);
            r.status = s.status; r.frames = s.frames;
        }
        else if(p->spsd) {
            struct kosdec_adx_spsd_result s = kosdec_adx_spsd_read(p->spsd, pcm, room);
            r.status = s.status; r.frames = s.frames;
        }
        else {
            struct kosdec_adx_sequence_result s = kosdec_adx_sequence_read(p->sequence, pcm, room);
            r.status = s.status;
            r.frames = s.frames;
        }
        if(!__atomic_load_n(&p->ready, __ATOMIC_RELAXED)) {
            bool ready = p->stream ? p->stream->prepared : p->joint ?
                p->joint->decoder.header_ready : p->wave ? p->wave->header_ready : p->au ? p->au->header_ready : p->aiff ? p->aiff->header_ready : p->spsd ? p->spsd->header_ready : p->sequence->format_ready;
            if(ready) {
                p->sample_rate = p->stream ? p->stream->decoder.sample_rate : p->joint ?
                    p->joint->decoder.sample_rate : p->wave ? p->wave->info.sample_rate : p->au ? p->au->info.sample_rate : p->aiff ? p->aiff->info.sample_rate : p->spsd ? p->spsd->info.sample_rate : p->sequence->sample_rate;
                p->channels = p->stream ? p->stream->decoder.channels : p->joint ?
                    p->joint->decoder.channels : p->wave ? p->wave->info.channels : p->au ? p->au->info.channels : p->aiff ? p->aiff->info.channels : p->spsd ? p->spsd->info.channels : p->sequence->channels;
                __atomic_store_n(&p->ready, 1, __ATOMIC_RELEASE);
            }
        }
        if(r.frames) {
            size_t index = head & (p->capacity - 1);
            size_t first = r.frames < p->capacity - index ? r.frames : p->capacity - index;
            memcpy(p->pcm + index * p->channels, pcm, first * p->channels * sizeof(*pcm));
            if(first < r.frames)
                memcpy(p->pcm, pcm + first * p->channels,
                       (r.frames - first) * p->channels * sizeof(*pcm));
            __atomic_store_n(&p->write_cursor, head + (uint32_t)r.frames, __ATOMIC_RELEASE);
        }
    }
    __atomic_store_n(&p->decoded_loops, p->stream ? (uint32_t)p->stream->wraps : 0,
                     __ATOMIC_RELEASE);
    if(r.status >= KOSDEC_ADX_DONE)
        __atomic_store_n(&p->terminal, r.status, __ATOMIC_RELEASE);
    return r;
}
struct kosdec_adx_pipe_result kosdec_adx_pipe_read(
    struct kosdec_adx_pipe *p, int16_t *output, size_t capacity) {
    struct kosdec_adx_pipe_result r = {.producer = KOSDEC_ADX_BAD_ARGUMENT};
    if(!valid(p) || (!output && capacity) ||
       (output && (uintptr_t)output % _Alignof(int16_t))) return r;
    /* Closure acquired first: a closed snapshot must include its final PCM.
       A racing close may report live/empty once; never a false completed drain. */
    r.producer = (enum kosdec_adx_status)__atomic_load_n(&p->terminal, __ATOMIC_ACQUIRE);
    if(__atomic_load_n(&p->ready, __ATOMIC_ACQUIRE)) {
        uint32_t head = __atomic_load_n(&p->write_cursor, __ATOMIC_ACQUIRE);
        uint32_t tail = __atomic_load_n(&p->read_cursor, __ATOMIC_RELAXED);
        size_t available = (uint32_t)(head - tail);
        size_t count = capacity < available ? capacity : available;
        size_t index = tail & (p->capacity - 1);
        size_t first = count < p->capacity - index ? count : p->capacity - index;
        if(first) memcpy(output, p->pcm + index * p->channels, first * p->channels * sizeof(*output));
        if(first < count) memcpy(output + first * p->channels, p->pcm,
                                (count - first) * p->channels * sizeof(*output));
        if(count) __atomic_store_n(&p->read_cursor, tail + (uint32_t)count, __ATOMIC_RELEASE);
        r.frames = count;
        r.queued_frames = available - count;
    }
    r.drained = r.producer >= KOSDEC_ADX_DONE && !r.queued_frames;
    r.starved = r.producer == KOSDEC_ADX_MORE && r.frames < capacity;
    return r;
}
