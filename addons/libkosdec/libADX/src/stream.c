/* Independent short-read staging and PCM cursor; no device ownership. */
#include <kosdec/adx_stream.h>
#include "adx_math.h"
#include "adx_filter.h"
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
enum kosdec_adx_status kosdec_adx_stream_set_linked(struct kosdec_adx_stream *s,bool enabled) {
    if(!valid(s) || s->terminal || s->byte_position || s->input_count || s->seeking)
        return KOSDEC_ADX_BAD_ARGUMENT;
    if(enabled && s->repeat) return KOSDEC_ADX_UNSUPPORTED;
    s->linked=s->follow_links=enabled;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_stream_release_link(struct kosdec_adx_stream *s) {
    if(!valid(s)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(!s->linked) return KOSDEC_ADX_UNSUPPORTED;
    if(s->terminal) return s->terminal;
    s->follow_links=false;
    return KOSDEC_ADX_MORE;
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
    if(s->linked && s->linked_entries && !s->follow_links && !s->prepared) {
        result.status=KOSDEC_ADX_DONE;goto terminal;
    }
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
        size_t take=available;
        if(s->linked) {
            size_t remaining=s->prepared?d->footer_bytes-d->footer_used:d->header_bytes-d->header_used;
            if(take>remaining) take=remaining;
        }
        struct kosdec_adx_result parsed = kosdec_adx_decode(d, input, take,
            NULL, 0, final && take==available);
        consume(s, parsed.consumed);
        result.status = parsed.status;
        if(s->linked && s->prepared &&
           (result.status==KOSDEC_ADX_NEED_END || result.status==KOSDEC_ADX_DONE)) {
            ++s->linked_entries;
            if(!s->follow_links || s->byte_position==s->input_bytes) {
                result.status=KOSDEC_ADX_DONE;goto terminal;
            }
            kosdec_adx_pcm_filter filter=d->filter;
            void *filter_context=d->filter_context;
            kosdec_adx_init(d);
            (void)kosdec_adx_set_filter(d,filter,filter_context);
            s->segment_start=s->byte_position;
            s->source_frame=0;
            s->prepared=false;
            s->cache_position=s->cache_frames=0;
            result.status=KOSDEC_ADX_MORE;
            return result;
        }
        if(result.status >= KOSDEC_ADX_DONE) goto terminal;
        if(!s->prepared && d->header_ready) {
            uint64_t body_end = d->header_bytes +
                ((uint64_t)d->total_frames + 31u) / 32u * 18u * d->channels;
            if(body_end + s->segment_start + 18u > s->input_bytes) {
                result.status = KOSDEC_ADX_TRUNCATED; goto terminal;
            }
            if(s->repeat && !d->loop.present) {
                result.status = KOSDEC_ADX_UNSUPPORTED; goto terminal;
            }
            if(s->linked) {
                if(d->loop.present || (s->linked_entries &&
                   (s->linked_rate!=d->sample_rate || s->linked_channels!=d->channels))) {
                    result.status=KOSDEC_ADX_UNSUPPORTED;goto terminal;
                }
                s->linked_rate=d->sample_rate;s->linked_channels=d->channels;
            }
            s->prepared = true;
        }
        /* A bounded linked prefix step may leave locally staged bytes. Do not
           report external starvation to the producer scheduler in that case. */
        if(s->linked && result.status==KOSDEC_ADX_NEED_INPUT && s->input_used<s->input_count)
            result.status=KOSDEC_ADX_MORE;
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
    if(!s->seeking)
        media_adx_filter_pcm(d->filter, d->filter_context, d->channels, output, frames);
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
    if(s->repeat || s->linked) return KOSDEC_ADX_UNSUPPORTED;
    kosdec_adx_pcm_filter filter = s->decoder.filter;
    void *context = s->decoder.filter_context;
    kosdec_adx_stream_init(s,s->read_at,s->context,s->input_bytes,false);
    kosdec_adx_set_filter(&s->decoder,filter,context);
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
