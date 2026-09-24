/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <dc/sound/adx_pipe.h>
#include <string.h>

/* This bridge must not silently acquire an out-of-line atomic library lock. */
_Static_assert(sizeof(uint32_t) == sizeof(unsigned int),
               "ADX pipe counters require 32-bit unsigned int");
_Static_assert(__GCC_ATOMIC_INT_LOCK_FREE == 2,
               "ADX pipe requires lock-free 32-bit atomics");

int snd_adx_pipe_init(snd_adx_pipe_t *p, int16_t *pcm,
                     size_t samples, size_t capacity) {
    if(!p || !pcm || ((uintptr_t)pcm % _Alignof(int16_t)) ||
       capacity < 32 || capacity > 1048576 || (capacity & (capacity - 1)) ||
       samples < 2 * capacity)
        return -1;
    memset(p, 0, sizeof(*p));
    snd_adx_init(&p->decoder);
    p->pcm = pcm;
    p->capacity = (uint32_t)capacity;
    return 0;
}

bool snd_adx_pipe_get_info(const snd_adx_pipe_t *p, snd_adx_info_t *info) {
    if(!p || !info || !__atomic_load_n(&p->ready, __ATOMIC_ACQUIRE))
        return false;
    *info = p->decoder.info;
    return true;
}

void snd_adx_pipe_request_cancel(snd_adx_pipe_t *p) {
    if(p)
        __atomic_store_n(&p->cancel_requested, 1, __ATOMIC_RELEASE);
}

snd_adx_result_t snd_adx_pipe_decode(snd_adx_pipe_t *p, const void *input,
                                    size_t bytes, bool final_input) {
    snd_adx_result_t r = { SND_ADX_BAD_ARGUMENT, 0, 0 };
    if(!p || (!input && bytes))
        return r;
    uint32_t terminal = __atomic_load_n(&p->terminal, __ATOMIC_RELAXED);
    if(terminal) {
        r.status = (snd_adx_status_t)terminal;
        return r;
    }
    if(__atomic_load_n(&p->cancel_requested, __ATOMIC_ACQUIRE)) {
        snd_adx_cancel(&p->decoder);
        r.status = SND_ADX_CANCELLED;
        __atomic_store_n(&p->terminal, r.status, __ATOMIC_RELEASE);
        return r;
    }

    uint32_t head = __atomic_load_n(&p->write_cursor, __ATOMIC_RELAXED);
    size_t space = 0;
    int16_t *destination = NULL;
    if(p->decoder.header_ready) {
        uint32_t tail = __atomic_load_n(&p->read_cursor, __ATOMIC_ACQUIRE);
        uint32_t index = head & (p->capacity - 1);
        space = p->capacity - (uint32_t)(head - tail);
        if(space > p->capacity - index)
            space = p->capacity - index;
        destination = p->pcm + (size_t)index * p->decoder.info.channels;
    }

    r = snd_adx_decode(&p->decoder, input, bytes, destination, space, final_input);
    /* Full groups are 32 frames and capacity is a power of two >= 32.
       Only the terminal group may be shorter, so a live producer never
       strands a group across the physical end of the ring. */
    if(r.frames)
        __atomic_store_n(&p->write_cursor, head + (uint32_t)r.frames, __ATOMIC_RELEASE);
    if(p->decoder.header_ready && !__atomic_load_n(&p->ready, __ATOMIC_RELAXED))
        __atomic_store_n(&p->ready, 1, __ATOMIC_RELEASE);
    if(r.status >= SND_ADX_DONE && r.status != SND_ADX_BAD_ARGUMENT)
        __atomic_store_n(&p->terminal, r.status, __ATOMIC_RELEASE);
    return r;
}

snd_adx_pipe_result_t snd_adx_pipe_read(snd_adx_pipe_t *p,
                                      int16_t *out, size_t capacity) {
    snd_adx_pipe_result_t r = { 0, 0, SND_ADX_BAD_ARGUMENT, false, false };
    if(!p || (!out && capacity))
        return r;
    /* Load terminal BEFORE head: observing closure must also observe all
       PCM published before it. A racing close may conservatively report
       live/empty once, but cannot falsely report a completed drain. */
    r.producer = (snd_adx_status_t)__atomic_load_n(&p->terminal, __ATOMIC_ACQUIRE);
    if(__atomic_load_n(&p->ready, __ATOMIC_ACQUIRE)) {
        uint32_t head = __atomic_load_n(&p->write_cursor, __ATOMIC_ACQUIRE);
        uint32_t tail = __atomic_load_n(&p->read_cursor, __ATOMIC_RELAXED);
        size_t available = (uint32_t)(head - tail);
        size_t count = capacity < available ? capacity : available;
        size_t index = tail & (p->capacity - 1);
        size_t first = count < p->capacity - index ? count : p->capacity - index;
        size_t channels = p->decoder.info.channels;
        if(first)
            memcpy(out, p->pcm + index * channels, first * channels * sizeof(*out));
        if(count > first)
            memcpy(out + first * channels, p->pcm,
                   (count - first) * channels * sizeof(*out));
        if(count)
            __atomic_store_n(&p->read_cursor, tail + (uint32_t)count, __ATOMIC_RELEASE);
        r.frames = count;
        r.queued_frames = available - count;
    }
    r.drained = r.producer >= SND_ADX_DONE && !r.queued_frames;
    r.starved = r.producer == SND_ADX_MORE && r.frames < capacity;
    return r;
}
