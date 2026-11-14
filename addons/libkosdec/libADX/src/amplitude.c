#include <kosdec/adx_amplitude.h>
#include <string.h>
#define AMPLITUDE_MAGIC UINT32_C(0x41445841)
_Static_assert(__GCC_ATOMIC_INT_LOCK_FREE == 2, "lock-free amplitude cursors");
static bool valid(const struct kosdec_adx_amplitude *a) {
    return a && a->magic == AMPLITUDE_MAGIC;
}
size_t kosdec_adx_amplitude_work_size(unsigned channels, size_t capacity) {
    if((channels != 1 && channels != 2) || capacity < 2 || capacity > 65536 ||
       (capacity & (capacity - 1))) return 0;
    return channels * capacity * sizeof(struct kosdec_adx_amplitude_record);
}
enum kosdec_adx_status kosdec_adx_amplitude_init(struct kosdec_adx_amplitude *a,
    struct kosdec_adx_amplitude_record *records, size_t count, unsigned channels,
    size_t capacity, uint32_t rate, uint32_t window) {
    size_t bytes = kosdec_adx_amplitude_work_size(channels, capacity);
    if(!a || !records || (uintptr_t)records % _Alignof(struct kosdec_adx_amplitude_record) || !bytes ||
       count < channels * capacity || !rate || rate > 48000 || !window ||
       window > INT32_MAX) return KOSDEC_ADX_BAD_ARGUMENT;
    memset(a, 0, sizeof(*a));
    a->magic = AMPLITUDE_MAGIC; a->records = records;
    a->channels = channels; a->capacity = (uint32_t)capacity;
    a->sample_rate = rate; a->window_frames = window;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_amplitude_status(const struct kosdec_adx_amplitude *a) {
    if(!valid(a)) return KOSDEC_ADX_BAD_ARGUMENT;
    uint32_t fault = __atomic_load_n(&a->fault, __ATOMIC_ACQUIRE);
    if(fault) return (enum kosdec_adx_status)fault;
    return __atomic_load_n(&a->sealed, __ATOMIC_ACQUIRE) ? KOSDEC_ADX_DONE : KOSDEC_ADX_MORE;
}
struct kosdec_adx_amplitude_result kosdec_adx_amplitude_feed(
    struct kosdec_adx_amplitude *a, unsigned channel, const int16_t *pcm,
    size_t frames, unsigned stride) {
    struct kosdec_adx_amplitude_result r = {KOSDEC_ADX_BAD_ARGUMENT, 0};
    if(!valid(a) || channel >= a->channels || (!pcm && frames) ||
       (pcm && (uintptr_t)pcm % _Alignof(int16_t)) || (stride != 1 && stride != 2)) return r;
    r.status = kosdec_adx_amplitude_status(a);
    if(r.status == KOSDEC_ADX_DONE) r.status = KOSDEC_ADX_BAD_ARGUMENT;
    if(r.status != KOSDEC_ADX_MORE) return r;
    struct kosdec_adx_amplitude_channel *c = &a->channel[channel];
    size_t budget = frames < KOSDEC_ADX_AMPLITUDE_BUDGET ? frames : KOSDEC_ADX_AMPLITUDE_BUDGET;
    if(budget > UINT64_MAX - c->position) { r.status = KOSDEC_ADX_UNSUPPORTED; return r; }
    uint32_t head = __atomic_load_n(&c->write_cursor, __ATOMIC_RELAXED);
    while(r.consumed < budget) {
        uint32_t tail = __atomic_load_n(&c->read_cursor, __ATOMIC_ACQUIRE);
        if((uint32_t)(head - tail) == a->capacity) {
            r.status = KOSDEC_ADX_NEED_OUTPUT;
            break;
        }
        size_t take = a->window_frames - c->used;
        if(take > budget - r.consumed) take = budget - r.consumed;
        for(size_t i = 0; i < take; ++i) {
            int32_t sample = pcm[(r.consumed + i) * stride];
            uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
            if(magnitude > c->peak) c->peak = magnitude;
        }
        c->used += (uint32_t)take; c->position += take; r.consumed += take;
        if(c->used == a->window_frames) {
            struct kosdec_adx_amplitude_record *slot = a->records +
                channel * a->capacity + (head & (a->capacity - 1));
            *slot = (struct kosdec_adx_amplitude_record){
                c->position - c->used, c->peak, a->sample_rate, c->used};
            c->used = c->peak = 0;
            __atomic_store_n(&c->write_cursor, ++head, __ATOMIC_RELEASE);
        }
    }
    return r;
}
void kosdec_adx_amplitude_filter(void *context, unsigned channel, int16_t *pcm,
                               size_t frames, unsigned stride) {
    struct kosdec_adx_amplitude *a = context;
    if(!valid(a)) return;
    struct kosdec_adx_amplitude_result r = kosdec_adx_amplitude_feed(a, channel, pcm, frames, stride);
    if(r.status != KOSDEC_ADX_MORE || r.consumed != frames) {
        uint32_t fault = r.status == KOSDEC_ADX_MORE ? KOSDEC_ADX_BAD_ARGUMENT : r.status;
        if(!__atomic_load_n(&a->fault, __ATOMIC_RELAXED))
            __atomic_store_n(&a->fault, fault, __ATOMIC_RELEASE);
    }
}
enum kosdec_adx_status kosdec_adx_amplitude_finish(struct kosdec_adx_amplitude *a) {
    if(!valid(a)) return KOSDEC_ADX_BAD_ARGUMENT;
    enum kosdec_adx_status status = kosdec_adx_amplitude_status(a);
    if(status != KOSDEC_ADX_MORE) return status;
    for(unsigned ch = 0; ch < a->channels; ++ch) a->channel[ch].used = a->channel[ch].peak = 0;
    __atomic_store_n(&a->sealed, 1, __ATOMIC_RELEASE);
    return KOSDEC_ADX_DONE;
}
static enum kosdec_adx_status record(struct kosdec_adx_amplitude *a, unsigned channel,
    uint64_t requested, bool select, struct kosdec_adx_amplitude_record *out) {
    if(!valid(a) || channel >= a->channels || !out) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_amplitude_channel *c = &a->channel[channel];
    uint32_t tail = __atomic_load_n(&c->read_cursor, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&c->write_cursor, __ATOMIC_ACQUIRE);
    if(tail == head) {
        enum kosdec_adx_status status = kosdec_adx_amplitude_status(a);
        /* A final seal/fault may have followed the first cursor snapshot. */
        head = __atomic_load_n(&c->write_cursor, __ATOMIC_ACQUIRE);
        if(tail == head) return status == KOSDEC_ADX_MORE ? KOSDEC_ADX_NEED_INPUT :
            status == KOSDEC_ADX_DONE ? KOSDEC_ADX_NEED_END : status;
    }
    for(;;) {
        *out = a->records[channel * a->capacity + (tail & (a->capacity - 1))];
        if(!select || (out->start_frame < requested && (uint32_t)(head - tail) > 1)) {
            __atomic_store_n(&c->read_cursor, ++tail, __ATOMIC_RELEASE);
            if(select) continue;
        }
        return KOSDEC_ADX_DONE;
    }
}
enum kosdec_adx_status kosdec_adx_amplitude_read(struct kosdec_adx_amplitude *a,
    unsigned channel, struct kosdec_adx_amplitude_record *out) {
    return record(a, channel, 0, false, out);
}
enum kosdec_adx_status kosdec_adx_amplitude_select(struct kosdec_adx_amplitude *a,
    unsigned channel, uint64_t requested, struct kosdec_adx_amplitude_record *out) {
    return record(a, channel, requested, true, out);
}
