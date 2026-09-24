/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <dc/sound/adx_input.h>
#include <string.h>

_Static_assert(sizeof(uint32_t) == sizeof(unsigned int), "32-bit counters required");
_Static_assert(__GCC_ATOMIC_INT_LOCK_FREE == 2, "lock-free counters required");

int snd_adx_input_init(snd_adx_input_t *q, uint8_t *storage, size_t capacity) {
    if(!q || !storage || capacity < 32 || capacity > 1048576 ||
       (capacity & (capacity - 1)))
        return -1;
    memset(q, 0, sizeof(*q));
    q->bytes = storage;
    q->capacity = (uint32_t)capacity;
    return 0;
}

snd_adx_input_end_t snd_adx_input_get_end(const snd_adx_input_t *q) {
    return q ? (snd_adx_input_end_t)__atomic_load_n(&q->end, __ATOMIC_ACQUIRE) :
               SND_ADX_INPUT_FAILED;
}

int snd_adx_input_close(snd_adx_input_t *q, snd_adx_input_end_t end) {
    if(!q || (end != SND_ADX_INPUT_EOF && end != SND_ADX_INPUT_FAILED))
        return -1;
    uint32_t old = __atomic_load_n(&q->end, __ATOMIC_RELAXED);
    if(old && old != (uint32_t)end)
        return -1;
    __atomic_store_n(&q->end, (uint32_t)end, __ATOMIC_RELEASE);
    return 0;
}

size_t snd_adx_input_write(snd_adx_input_t *q, const void *bytes, size_t size) {
    if(!q || (!bytes && size) || __atomic_load_n(&q->end, __ATOMIC_RELAXED))
        return 0;
    uint32_t head = __atomic_load_n(&q->write_cursor, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&q->read_cursor, __ATOMIC_ACQUIRE);
    size_t free_bytes = q->capacity - (uint32_t)(head - tail);
    size_t count = size < free_bytes ? size : free_bytes;
    size_t index = head & (q->capacity - 1);
    size_t first = count < q->capacity - index ? count : q->capacity - index;
    if(first)
        memcpy(q->bytes + index, bytes, first);
    if(count > first)
        memcpy(q->bytes, (const uint8_t *)bytes + first, count - first);
    if(count)
        __atomic_store_n(&q->write_cursor, head + (uint32_t)count, __ATOMIC_RELEASE);
    return count;
}

snd_adx_input_result_t snd_adx_input_step(snd_adx_input_t *q, snd_adx_pipe_t *pipe) {
    snd_adx_input_result_t r = { { SND_ADX_BAD_ARGUMENT, 0, 0 }, SND_ADX_INPUT_OPEN };
    if(!q || !pipe)
        return r;
    /* Closing publishes all preceding loader writes; read closure before the
       head snapshot so EOF can never hide the final bytes. */
    r.input = snd_adx_input_get_end(q);
    if(r.input == SND_ADX_INPUT_FAILED) {
        snd_adx_pipe_request_cancel(pipe);
        r.codec = snd_adx_pipe_decode(pipe, NULL, 0, false);
        return r;
    }
    uint32_t head = __atomic_load_n(&q->write_cursor, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&q->read_cursor, __ATOMIC_RELAXED);
    size_t available = (uint32_t)(head - tail);
    size_t index = tail & (q->capacity - 1);
    size_t span = available < q->capacity - index ? available : q->capacity - index;
    r.codec = snd_adx_pipe_decode(pipe, q->bytes + index, span,
                             r.input == SND_ADX_INPUT_EOF && span == available);
    if(r.codec.consumed)
        __atomic_store_n(&q->read_cursor, tail + (uint32_t)r.codec.consumed, __ATOMIC_RELEASE);
    /* A physical wrap is not starvation. Another span is already available;
       asking the service to park here could lose the final EOF drain wake. */
    if(r.codec.status == SND_ADX_NEED_INPUT && available > r.codec.consumed)
        r.codec.status = SND_ADX_MORE;
    return r;
}
