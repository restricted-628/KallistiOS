#include "adx_container_internal.h"
#include <string.h>
#include "adx_filter.h"

#define AU_MAGIC UINT32_C(0x41445855)
static bool valid(const struct kosdec_adx_au *a) { return a && a->magic == AU_MAGIC; }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
enum kosdec_adx_status kosdec_adx_au_init(struct kosdec_adx_au *a,
    kosdec_adx_read_at read_at, void *context, uint32_t bytes) {
    if(!a || !read_at) return KOSDEC_ADX_BAD_ARGUMENT;
    memset(a, 0, sizeof(*a));
    a->magic = AU_MAGIC; a->read_at = read_at; a->context = context; a->input_bytes = bytes;
    return a->terminal = bytes < 24 ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_MORE;
}
static enum kosdec_adx_status fill(struct kosdec_adx_au *a, unsigned n) {
    enum kosdec_adx_status status =
        media_adx_container_fill(a->read_at, a->context, a->buffer, &a->used, &a->offset, n);
    if(status >= KOSDEC_ADX_DONE)
        a->terminal = status;
    return status;
}
static unsigned pcm_request(const struct kosdec_adx_au *a) {
    uint32_t remaining = a->info.data_offset + a->info.data_bytes - a->offset;
    return media_adx_container_pcm_request(MEDIA_ADX_CONTAINER_CHUNK - a->used, remaining);
}

enum kosdec_adx_status media_adx_au_step_file(struct kosdec_adx_au *a) {
    if(!valid(a)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(a->terminal) return a->terminal;
    if(!a->header_ready) {
        unsigned n = 24 - a->used;
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

static enum kosdec_adx_status header(struct kosdec_adx_au *a, bool staged) {
    if(a->used < 24) {
        if(staged) return KOSDEC_ADX_NEED_INPUT;
        enum kosdec_adx_status status = fill(a, 24 - a->used);
        if(status != KOSDEC_ADX_MORE || a->used < 24) return status;
    }
    if(memcmp(a->buffer, ".snd", 4) && memcmp(a->buffer, ".sd\0", 4))
        return a->terminal = KOSDEC_ADX_UNSUPPORTED;
    uint32_t offset = be32(a->buffer + 4), bytes = be32(a->buffer + 8);
    uint32_t encoding = be32(a->buffer + 12), rate = be32(a->buffer + 16);
    uint32_t channels = be32(a->buffer + 20);
    if(encoding < 1 || encoding > 3 || (channels != 1 && channels != 2))
        return a->terminal = KOSDEC_ADX_UNSUPPORTED;
    if(!rate || offset < 24 || offset > a->input_bytes)
        return a->terminal = KOSDEC_ADX_INVALID;
    if(bytes == UINT32_MAX) bytes = a->input_bytes - offset;
    unsigned frame_bytes = channels * (encoding == 3 ? 2u : 1u);
    if(bytes > a->input_bytes - offset || bytes % frame_bytes)
        return a->terminal = KOSDEC_ADX_INVALID;
    a->info = (struct kosdec_adx_au_info){rate, bytes / frame_bytes, offset, bytes,
        (uint8_t)channels, (uint8_t)(encoding == 3 ? 16 : 8), (uint8_t)encoding};
    a->frame_bytes = frame_bytes; a->offset = offset; a->used = 0; a->header_ready = true;
    return a->terminal = bytes ? KOSDEC_ADX_MORE : KOSDEC_ADX_DONE;
}
static int expand_mulaw(uint8_t code) {
    unsigned v = code ^ 255u;
    int magnitude = (int)(((v & 15u) * 8u + 132u) << ((v >> 4) & 7u)) - 132;
    return v & 128u ? -magnitude : magnitude;
}
struct kosdec_adx_au_result media_adx_au_read(struct kosdec_adx_au *a,
                                             int16_t *out, size_t capacity, bool staged) {
    struct kosdec_adx_au_result result = {KOSDEC_ADX_BAD_ARGUMENT, 0};
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
        int value;
        if(a->info.encoding == 1) value = expand_mulaw(input[i]);
        else if(a->info.encoding == 2) {
            unsigned v = input[i]; value = (v < 128 ? (int)v : (int)v - 256) * 256;
        } else {
            unsigned v = (unsigned)input[2*i] << 8 | input[2*i+1];
            value = v < 32768 ? (int)v : (int)v - 65536;
        }
        out[i] = (int16_t)value;
    }
    if(frames) media_adx_filter_pcm(a->filter, a->filter_context, a->info.channels, out, frames);
    a->input_pos += (unsigned)frames * a->frame_bytes;
    a->frames_done += (uint32_t)frames; result.frames = frames;
    result.status = a->frames_done == a->info.total_frames ?
        (a->terminal = KOSDEC_ADX_DONE) : KOSDEC_ADX_MORE;
    return result;
}
struct kosdec_adx_au_result kosdec_adx_au_read(struct kosdec_adx_au *a,
                                               int16_t *out, size_t capacity) {
    return media_adx_au_read(a, out, capacity, false);
}
enum kosdec_adx_status kosdec_adx_au_get_info(const struct kosdec_adx_au *a,
                                           struct kosdec_adx_au_info *out) {
    if(!valid(a) || !out) return KOSDEC_ADX_BAD_ARGUMENT;
    if(!a->header_ready) return a->terminal ? a->terminal : KOSDEC_ADX_NEED_INPUT;
    *out = a->info; return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_au_seek(struct kosdec_adx_au *a, uint32_t frame) {
    if(!valid(a) || !a->header_ready || frame > a->info.total_frames) return KOSDEC_ADX_BAD_ARGUMENT;
    a->offset = a->info.data_offset + frame * a->frame_bytes; a->frames_done = frame; a->used = a->input_pos = 0;
    a->staged_error = KOSDEC_ADX_MORE;
    a->terminal = frame == a->info.total_frames ? KOSDEC_ADX_DONE : KOSDEC_ADX_MORE;
    return KOSDEC_ADX_DONE;
}
void kosdec_adx_au_cancel(struct kosdec_adx_au *a) {
    if(valid(a) && !a->terminal) a->terminal = KOSDEC_ADX_CANCELLED;
}
enum kosdec_adx_status kosdec_adx_au_set_filter(struct kosdec_adx_au *a,
    kosdec_adx_pcm_filter filter, void *context) {
    if(!valid(a)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(a->terminal) return a->terminal == KOSDEC_ADX_DONE ? KOSDEC_ADX_BAD_ARGUMENT : a->terminal;
    a->filter = filter; a->filter_context = filter ? context : NULL;
    return KOSDEC_ADX_DONE;
}
