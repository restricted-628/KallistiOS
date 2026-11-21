#include "adx_container_internal.h"
#include <string.h>
#include "adx_filter.h"

#define SPSD_MAGIC UINT32_C(0x41445853)
static bool valid(const struct kosdec_adx_spsd *a) { return a && a->magic == SPSD_MAGIC; }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
enum kosdec_adx_status kosdec_adx_spsd_init(struct kosdec_adx_spsd *a,
    kosdec_adx_read_at read_at, void *context, uint32_t bytes) {
    if(!a || !read_at) return KOSDEC_ADX_BAD_ARGUMENT;
    memset(a, 0, sizeof(*a));
    a->magic = SPSD_MAGIC; a->read_at = read_at; a->context = context; a->input_bytes = bytes;
    return a->terminal = bytes < 64 ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_MORE;
}
static enum kosdec_adx_status fill(struct kosdec_adx_spsd *a, unsigned n) {
    enum kosdec_adx_status status =
        media_adx_container_fill(a->read_at, a->context, a->buffer, &a->used, &a->offset, n);
    if(status >= KOSDEC_ADX_DONE)
        a->terminal = status;
    return status;
}
static unsigned pcm_request(const struct kosdec_adx_spsd *a) {
    uint32_t remaining = a->info.data_offset + a->info.data_bytes - a->offset;
    return media_adx_container_pcm_request(MEDIA_ADX_CONTAINER_CHUNK - a->used, remaining);
}

enum kosdec_adx_status media_adx_spsd_step_file(struct kosdec_adx_spsd *a) {
    if(!valid(a)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(a->terminal) return a->terminal;
    if(!a->header_ready) {
        unsigned n = 64 - a->used;
        return n ? fill(a, n) : KOSDEC_ADX_MORE;
    }

    enum kosdec_adx_status status = a->staged_error;
    if(!status) {
        uint32_t end = a->info.data_offset + a->info.data_bytes;
        if(a->offset == end)
            return KOSDEC_ADX_MORE;
        media_adx_container_compact(a->buffer, &a->used, &a->input_pos);
        status = media_adx_container_stage(a->read_at, a->context, a->buffer, sizeof(a->buffer),
                                           &a->used, &a->offset, end);
        if(status < KOSDEC_ADX_DONE)
            return status;
        a->staged_error = status;
    }
    /* Keep the complete prefix available to DECODER before publishing the
       failure. An incomplete final unit cannot be reconstructed. */
    if(a->used - a->input_pos >= a->frame_bytes)
        return KOSDEC_ADX_MORE;
    return a->terminal = status;
}

static enum kosdec_adx_status header(struct kosdec_adx_spsd *a, bool staged) {
    if(a->used < 64) {
        if(staged) return KOSDEC_ADX_NEED_INPUT;
        enum kosdec_adx_status status = fill(a, 64 - a->used);
        if(status != KOSDEC_ADX_MORE || a->used < 64) return status;
    }
    const uint8_t *p = a->buffer;
    if(memcmp(p, "SPSD", 4) || p[4] > 1 || p[5] != 1 || p[6] || p[7] != 4 ||
       p[8] || (p[9] & 0x7eu) || p[10] || p[11])
        return a->terminal = KOSDEC_ADX_UNSUPPORTED;
    uint32_t channel_bytes = le32(p + 12);
    unsigned rate = (unsigned)p[42] | (unsigned)p[43] << 8;
    unsigned channels = (p[9] & 1u) + 1;
    uint32_t available = a->input_bytes - 64;
    if(!rate || (channel_bytes & 1u) || channel_bytes > available / channels)
        return a->terminal = KOSDEC_ADX_INVALID;
    uint32_t bytes = channel_bytes * channels;
    /* An exact extent distinguishes the supported stereo frame count from
       aggregate-length records and leaves no room for an undeclared tail. */
    if(channels == 2 && bytes != available)
        return a->terminal = KOSDEC_ADX_INVALID;
    a->info = (struct kosdec_adx_spsd_info){rate, channel_bytes / 2, 64, bytes,
                                           (uint8_t)channels, 16};
    a->frame_bytes = 2 * channels;
    a->offset = 64;
    a->used = 0;
    a->header_ready = true;
    return a->terminal = bytes ? KOSDEC_ADX_MORE : KOSDEC_ADX_DONE;
}
struct kosdec_adx_spsd_result media_adx_spsd_read(struct kosdec_adx_spsd *a,
                                             int16_t *out, size_t capacity, bool staged) {
    struct kosdec_adx_spsd_result result = {KOSDEC_ADX_BAD_ARGUMENT, 0};
    if(!valid(a) || (!out && capacity) || (out && (uintptr_t)out % _Alignof(int16_t))) return result;
    if(a->terminal) { result.status = a->terminal; return result; }
    if(!capacity) { result.status = KOSDEC_ADX_NEED_OUTPUT; return result; }
    if(!a->header_ready) { result.status = header(a, staged); return result; }
    if(a->used - a->input_pos < a->frame_bytes) {
        if(a->staged_error) {
            result.status = a->terminal = a->staged_error;
            return result;
        }
        if(staged) { result.status = KOSDEC_ADX_NEED_INPUT; return result; }
        media_adx_container_compact(a->buffer, &a->used, &a->input_pos);
        unsigned n = pcm_request(a);
        if(!n) { result.status = a->terminal = KOSDEC_ADX_TRUNCATED; return result; }
        result.status = fill(a, n);
        if(result.status != KOSDEC_ADX_MORE) return result;
    }
    size_t frames = (a->used - a->input_pos) / a->frame_bytes;
    if(frames > capacity) frames = capacity;
    if(frames > 32) frames = 32;
    const uint8_t *input = a->buffer + a->input_pos;
    for(size_t i = 0; i < frames * a->info.channels; ++i) {
        unsigned v = (unsigned)input[2*i] | (unsigned)input[2*i+1] << 8;
        int value = v < 32768 ? (int)v : (int)v - 65536;
        out[i] = (int16_t)value;
    }
    if(frames) media_adx_filter_pcm(a->filter, a->filter_context, a->info.channels, out, frames);
    a->input_pos += (unsigned)frames * a->frame_bytes;
    a->frames_done += (uint32_t)frames; result.frames = frames;
    result.status = a->frames_done == a->info.total_frames ?
        (a->terminal = KOSDEC_ADX_DONE) : KOSDEC_ADX_MORE;
    return result;
}
struct kosdec_adx_spsd_result kosdec_adx_spsd_read(struct kosdec_adx_spsd *a,
                                               int16_t *out, size_t capacity) {
    return media_adx_spsd_read(a, out, capacity, false);
}
enum kosdec_adx_status kosdec_adx_spsd_get_info(const struct kosdec_adx_spsd *a,
                                           struct kosdec_adx_spsd_info *out) {
    if(!valid(a) || !out) return KOSDEC_ADX_BAD_ARGUMENT;
    if(!a->header_ready) return a->terminal ? a->terminal : KOSDEC_ADX_NEED_INPUT;
    *out = a->info; return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_spsd_seek(struct kosdec_adx_spsd *a, uint32_t frame) {
    if(!valid(a) || !a->header_ready || frame > a->info.total_frames) return KOSDEC_ADX_BAD_ARGUMENT;
    a->offset = a->info.data_offset + frame * a->frame_bytes; a->frames_done = frame; a->used = a->input_pos = 0;
    a->staged_error = KOSDEC_ADX_MORE;
    a->terminal = frame == a->info.total_frames ? KOSDEC_ADX_DONE : KOSDEC_ADX_MORE;
    return KOSDEC_ADX_DONE;
}
void kosdec_adx_spsd_cancel(struct kosdec_adx_spsd *a) {
    if(valid(a) && !a->terminal) a->terminal = KOSDEC_ADX_CANCELLED;
}
enum kosdec_adx_status kosdec_adx_spsd_set_filter(struct kosdec_adx_spsd *a,
    kosdec_adx_pcm_filter filter, void *context) {
    if(!valid(a)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(a->terminal) return a->terminal == KOSDEC_ADX_DONE ? KOSDEC_ADX_BAD_ARGUMENT : a->terminal;
    a->filter = filter; a->filter_context = filter ? context : NULL;
    return KOSDEC_ADX_DONE;
}
