/* Independently written resident PCM cursor and carried-history repeat policy. */
#include <kosdec/adx_memory.h>
#include "adx_math.h"
#include <string.h>
#define MEMORY_MAGIC UINT32_C(0x4144584d)
static bool valid(const struct kosdec_adx_memory *r) {
    return r && r->magic == MEMORY_MAGIC;
}
void kosdec_adx_memory_init(struct kosdec_adx_memory *r,
                          const uint8_t *input, size_t bytes, bool repeat) {
    if(!r) return;
    memset(r, 0, sizeof(*r));
    r->magic = MEMORY_MAGIC;
    kosdec_adx_init(&r->decoder);
    r->input = input;
    r->input_bytes = bytes;
    r->repeat = repeat;
    if(!input && bytes) r->terminal = KOSDEC_ADX_BAD_ARGUMENT;
}
void kosdec_adx_memory_pause(struct kosdec_adx_memory *r, bool paused) {
    if(valid(r) && !r->terminal) r->paused = paused;
}
void kosdec_adx_memory_release_loop(struct kosdec_adx_memory *r) {
    if(valid(r) && !r->terminal) r->repeat = false;
}
void kosdec_adx_memory_cancel(struct kosdec_adx_memory *r) {
    if(valid(r) && !r->terminal) r->terminal = KOSDEC_ADX_CANCELLED;
}
struct kosdec_adx_memory_result kosdec_adx_memory_read(
    struct kosdec_adx_memory *r, int16_t *output, size_t capacity) {
    struct kosdec_adx_memory_result result = {KOSDEC_ADX_BAD_ARGUMENT, 0};
    if(!valid(r) || (!output && capacity) ||
       (output && (uintptr_t)output % _Alignof(int16_t))) return result;
    if(r->terminal) { result.status = r->terminal; return result; }
    result.status = KOSDEC_ADX_MORE;
    if(r->paused) return result;
    struct kosdec_adx_decoder *d = &r->decoder;
    if(!r->prepared) {
        const uint8_t *p = r->input ? r->input + r->byte_position : NULL;
        struct kosdec_adx_result decoded = kosdec_adx_decode(d, p,
            r->input_bytes - r->byte_position, NULL, 0, true);
        r->byte_position += decoded.consumed;
        result.status = decoded.status;
        if(result.status >= KOSDEC_ADX_DONE) goto terminal;
        if(d->header_ready) {
            uint64_t body_end = d->header_bytes +
                ((uint64_t)d->total_frames + 31u) / 32u * 18u * d->channels;
            if(body_end + 18u > r->input_bytes) {
                result.status = KOSDEC_ADX_TRUNCATED; goto terminal;
            }
            if(r->repeat && !d->loop.present) {
                result.status = KOSDEC_ADX_UNSUPPORTED; goto terminal;
            }
            r->prepared = true;
        }
        return result;
    }
    /* Footer work is independent of output capacity, including empty files. */
    if(r->source_frame == d->total_frames && !r->repeat) {
        d->frames_done = d->total_frames;
        struct kosdec_adx_result decoded = kosdec_adx_decode(d,
            r->input + r->byte_position, r->input_bytes - r->byte_position,
            NULL, 0, true);
        r->byte_position += decoded.consumed;
        result.status = decoded.status;
        if(result.status >= KOSDEC_ADX_DONE) goto terminal;
        return result;
    }
    if(!capacity) { result.status = KOSDEC_ADX_NEED_OUTPUT; return result; }
    if(r->repeat && r->source_frame == d->loop.end_frame) {
        r->source_frame = d->loop.start_frame;
        r->byte_position = d->loop.start_byte;
        r->cache_position = r->cache_frames = 0;
        ++r->wraps;
    }
    if(r->cache_position == r->cache_frames) {
        const uint8_t *block = r->input + r->byte_position;
        for(unsigned ch = 0; ch < d->channels; ++ch)
            if(block[ch * 18] & 0x80) {
                result.status = KOSDEC_ADX_INVALID; goto terminal;
            }
        for(unsigned ch = 0; ch < d->channels; ++ch) {
            int clips = media_adx_channel_group(block + ch * 18, 32,
                d->coefficient, d->history[ch], r->pcm + ch, d->channels);
            if(clips < 0) { result.status = KOSDEC_ADX_INVALID; goto terminal; }
            d->clips += (uint32_t)clips;
        }
        r->byte_position += 18u * d->channels;
        r->cache_position = 0;
        r->cache_frames = 32;
    }
    uint32_t endpoint = r->repeat ? d->loop.end_frame : d->total_frames;
    size_t frames = r->cache_frames - r->cache_position;
    if(frames > endpoint - r->source_frame) frames = endpoint - r->source_frame;
    if(frames > capacity) frames = capacity;
    memcpy(output, r->pcm + r->cache_position * d->channels,
           frames * d->channels * sizeof(*output));
    r->cache_position += (unsigned)frames;
    r->source_frame += (uint32_t)frames;
    r->returned_frames += frames;
    result.frames = frames;
    return result;
terminal:
    r->terminal = result.status;
    return result;
}
