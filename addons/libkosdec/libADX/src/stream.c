/* Independent short-read staging and PCM cursor; no device ownership. */
#include <kosdec/adx_stream.h>
#include "adx_math.h"
#include <string.h>
#define STREAM_MAGIC UINT32_C(0x41445853)
static bool valid(const struct kosdec_adx_stream *s) {
    return s && s->magic == STREAM_MAGIC;
}
void kosdec_adx_stream_init(struct kosdec_adx_stream *s, kosdec_adx_read_at read_at,
                          void *context, uint32_t bytes, bool repeat) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    s->magic = STREAM_MAGIC;
    kosdec_adx_init(&s->decoder);
    s->read_at = read_at; s->context = context;
    s->input_bytes = bytes; s->repeat = repeat;
    if(!read_at) s->terminal = KOSDEC_ADX_BAD_ARGUMENT;
}
void kosdec_adx_stream_pause(struct kosdec_adx_stream *s, bool paused) {
    if(valid(s) && !s->terminal) s->paused = paused;
}
void kosdec_adx_stream_release_loop(struct kosdec_adx_stream *s) {
    if(valid(s) && !s->terminal) s->repeat = false;
}
void kosdec_adx_stream_cancel(struct kosdec_adx_stream *s) {
    if(valid(s) && !s->terminal) s->terminal = KOSDEC_ADX_CANCELLED;
}
static void consume(struct kosdec_adx_stream *s, size_t bytes) {
    s->input_used += (unsigned)bytes;
    s->byte_position += (uint32_t)bytes;
}
static struct kosdec_adx_stream_result read_step(
    struct kosdec_adx_stream *s, int16_t *output, size_t capacity) {
    struct kosdec_adx_stream_result result = {KOSDEC_ADX_BAD_ARGUMENT, 0};
    if(!valid(s) || (!output && capacity) ||
       (output && (uintptr_t)output % _Alignof(int16_t))) return result;
    if(s->terminal) { result.status = s->terminal; return result; }
    result.status = KOSDEC_ADX_MORE;
    if(s->paused) return result;
    if(!capacity) { result.status = KOSDEC_ADX_NEED_OUTPUT; return result; }
    struct kosdec_adx_decoder *d = &s->decoder;
    if(s->prepared && s->repeat && s->source_frame == d->loop.end_frame) {
        s->byte_position = d->loop.start_byte;
        s->source_frame = d->loop.start_frame;
        s->input_used = s->input_count = 0;
        s->cache_position = s->cache_frames = 0;
        ++s->wraps;
    }
    if(s->prepared && s->source_frame < d->total_frames &&
       s->cache_position < s->cache_frames) goto emit;
    if(s->input_used == s->input_count && s->byte_position < s->input_bytes) {
        size_t request = s->input_bytes - s->byte_position;
        if(request > sizeof(s->input)) request = sizeof(s->input);
        struct kosdec_adx_input_result in = s->read_at(s->context,
            s->byte_position, s->input, request);
        if(in.status == KOSDEC_ADX_INPUT_DATA && in.bytes && in.bytes <= request) {
            s->input_used = 0;
            s->input_count = (unsigned)in.bytes;
        }
        else if(in.status == KOSDEC_ADX_INPUT_WAIT && !in.bytes) {
            result.status = KOSDEC_ADX_NEED_INPUT; return result;
        }
        else {
            result.status = in.status == KOSDEC_ADX_INPUT_END && !in.bytes ?
                KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_IO_ERROR;
            goto terminal;
        }
    }
    size_t available = s->input_count - s->input_used;
    const uint8_t *input = s->input + s->input_used;
    bool final = available == s->input_bytes - s->byte_position;
    if(!s->prepared || s->source_frame == d->total_frames) {
        if(s->prepared) d->frames_done = d->total_frames;
        struct kosdec_adx_result parsed = kosdec_adx_decode(d, input, available,
            NULL, 0, final);
        consume(s, parsed.consumed);
        result.status = parsed.status;
        if(result.status >= KOSDEC_ADX_DONE) goto terminal;
        if(!s->prepared && d->header_ready) {
            uint64_t body_end = d->header_bytes +
                ((uint64_t)d->total_frames + 31u) / 32u * 18u * d->channels;
            if(body_end + 18u > s->input_bytes) {
                result.status = KOSDEC_ADX_TRUNCATED; goto terminal;
            }
            if(s->repeat && !d->loop.present) {
                result.status = KOSDEC_ADX_UNSUPPORTED; goto terminal;
            }
            s->prepared = true;
        }
        return result;
    }
    size_t bytes = 18u * d->channels - d->block_used;
    if(bytes > available) bytes = available;
    memcpy(d->block + d->block_used, input, bytes);
    d->block_used += (uint8_t)bytes;
    consume(s, bytes);
    if(d->block_used != 18u * d->channels) return result;
    for(unsigned ch = 0; ch < d->channels; ++ch)
        if(d->block[ch * 18] & 0x80) {
            result.status = KOSDEC_ADX_INVALID; goto terminal;
        }
    for(unsigned ch = 0; ch < d->channels; ++ch) {
        int clips = media_adx_channel_group(d->block + ch * 18, 32,
            d->coefficient, d->history[ch], s->pcm + ch, d->channels);
        if(clips < 0) { result.status = KOSDEC_ADX_INVALID; goto terminal; }
        d->clips += (uint32_t)clips;
    }
    d->block_used = 0;
    s->cache_position = 0; s->cache_frames = 32;
emit:;
    uint32_t endpoint = s->repeat ? d->loop.end_frame : d->total_frames;
    size_t frames = s->cache_frames - s->cache_position;
    if(frames > endpoint - s->source_frame) frames = endpoint - s->source_frame;
    if(frames > capacity) frames = capacity;
    memcpy(output, s->pcm + s->cache_position * d->channels,
           frames * d->channels * sizeof(*output));
    s->cache_position += (unsigned)frames;
    s->source_frame += (uint32_t)frames; s->returned_frames += frames;
    result.frames = frames;
    return result;
terminal:
    s->terminal = result.status;
    return result;
}
struct kosdec_adx_stream_result kosdec_adx_stream_read(
    struct kosdec_adx_stream *s, int16_t *output, size_t capacity) {
    if(valid(s) && s->seeking)
        return (struct kosdec_adx_stream_result){KOSDEC_ADX_BAD_ARGUMENT,0};
    return read_step(s,output,capacity);
}
enum kosdec_adx_status kosdec_adx_stream_seek_begin(struct kosdec_adx_stream *s, uint32_t frame) {
    if(!valid(s) || !s->read_at || frame > KOSDEC_ADX_MAX_FRAMES ||
       (s->prepared && frame > s->decoder.total_frames)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->repeat) return KOSDEC_ADX_UNSUPPORTED;
    kosdec_adx_stream_init(s,s->read_at,s->context,s->input_bytes,false);
    s->seek_frame = frame; s->seeking = true;
    return KOSDEC_ADX_MORE;
}
enum kosdec_adx_status kosdec_adx_stream_seek_step(struct kosdec_adx_stream *s) {
    if(!valid(s)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->terminal) return s->terminal;
    if(!s->seeking) return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->paused) return KOSDEC_ADX_MORE;
    int16_t discarded[64];
    size_t frames = s->prepared ? s->seek_frame - s->source_frame : 32;
    if(frames > 32) frames = 32;
    struct kosdec_adx_stream_result r = read_step(s,discarded,frames);
    s->returned_frames = 0;
    if(r.status >= KOSDEC_ADX_DONE) return r.status;
    if(s->prepared) {
        if(s->seek_frame > s->decoder.total_frames) {
            s->terminal = KOSDEC_ADX_BAD_ARGUMENT;
            return s->terminal;
        }
        if(s->source_frame == s->seek_frame) {
            s->seeking = false;
            return KOSDEC_ADX_DONE;
        }
    }
    return r.status == KOSDEC_ADX_NEED_OUTPUT ? KOSDEC_ADX_MORE : r.status;
}
