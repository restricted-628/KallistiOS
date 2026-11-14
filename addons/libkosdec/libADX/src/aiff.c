#include <kosdec/adx_aiff.h>
#include <string.h>
#include "adx_filter.h"
#define AIFF_MAGIC UINT32_C(0x41445849)
enum { FORM_HEADER, CHUNK_HEADER, FORMAT_BODY, SOUND_BODY, PCM_BODY };
static bool valid(const struct kosdec_adx_aiff *w) { return w && w->magic == AIFF_MAGIC; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] << 8 | (unsigned)p[1]); }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
enum kosdec_adx_status kosdec_adx_aiff_init(struct kosdec_adx_aiff *w,
    kosdec_adx_read_at read_at, void *context, uint32_t bytes) {
    if(!w || !read_at) return KOSDEC_ADX_BAD_ARGUMENT;
    memset(w, 0, sizeof(*w));w->magic = AIFF_MAGIC;w->read_at = read_at;w->context = context;
    w->input_bytes = bytes;w->need = 12;
    return w->terminal = bytes < 12 ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_MORE;
}
static enum kosdec_adx_status fill(struct kosdec_adx_aiff *w, unsigned n) {
    struct kosdec_adx_input_result in = w->read_at(w->context, w->offset, w->buffer + w->used, n);
    if(in.status == KOSDEC_ADX_INPUT_WAIT && !in.bytes) return KOSDEC_ADX_NEED_INPUT;
    if(in.status != KOSDEC_ADX_INPUT_DATA || !in.bytes || in.bytes > n)
        return w->terminal = in.status == KOSDEC_ADX_INPUT_END && !in.bytes ?
            KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_IO_ERROR;
    w->used += (unsigned)in.bytes;w->offset += (uint32_t)in.bytes;
    return KOSDEC_ADX_MORE;
}
static enum kosdec_adx_status admit(struct kosdec_adx_aiff *w) {
    if(!w->format_ready || (!w->sound_ready && w->info.total_frames))
        return w->terminal = KOSDEC_ADX_INVALID;
    uint64_t bytes = (uint64_t)w->info.total_frames * w->frame_bytes;
    if(bytes > w->sound_bytes) return w->terminal = KOSDEC_ADX_INVALID;
    w->info.data_bytes = (uint32_t)bytes;
    w->offset = w->info.data_offset; w->used = 0; w->header_ready = true; w->phase = PCM_BODY;
    return w->terminal = bytes ? KOSDEC_ADX_MORE : KOSDEC_ADX_DONE;
}
static enum kosdec_adx_status sample_rate(const uint8_t *p, uint32_t *out) {
    unsigned exponent = be16(p);
    uint64_t mantissa = (uint64_t)be32(p + 2) << 32 | be32(p + 6);
    if(exponent & 0x8000u || !exponent || exponent == 0x7fffu || !(mantissa >> 63))
        return KOSDEC_ADX_INVALID;
    if(exponent < 16383 || exponent > 16414) return KOSDEC_ADX_UNSUPPORTED;
    unsigned shift = 63 - (exponent - 16383);
    if(mantissa & ((UINT64_C(1) << shift) - 1)) return KOSDEC_ADX_UNSUPPORTED;
    *out = (uint32_t)(mantissa >> shift);
    return KOSDEC_ADX_MORE;
}
static enum kosdec_adx_status header(struct kosdec_adx_aiff *w) {
    if(w->phase == CHUNK_HEADER && !w->used) {
        if(w->offset == w->form_end) return admit(w);
        if(w->offset > w->form_end || w->form_end - w->offset < 8)
            return w->terminal = KOSDEC_ADX_INVALID;
    }
    enum kosdec_adx_status status = fill(w, w->need - w->used);
    if(status != KOSDEC_ADX_MORE || w->used < w->need) return status;
    w->used = 0;
    if(w->phase == FORM_HEADER) {
        if(memcmp(w->buffer, "FORM", 4) || memcmp(w->buffer + 8, "AIFF", 4))
            return w->terminal = KOSDEC_ADX_UNSUPPORTED;
        uint32_t size = be32(w->buffer + 4);
        if(size < 4 || size > w->input_bytes - 8) return w->terminal = KOSDEC_ADX_INVALID;
        w->form_end = size + 8; w->phase = CHUNK_HEADER; w->need = 8;
    } else if(w->phase == CHUNK_HEADER) {
        uint32_t size = be32(w->buffer + 4), remaining = w->form_end - w->offset;
        if(size > remaining || (size & 1u) > remaining - size)
            return w->terminal = KOSDEC_ADX_INVALID;
        w->next_chunk = w->offset + size + (size & 1u);
        if(!memcmp(w->buffer, "COMM", 4)) {
            if(w->format_ready || size < 18) return w->terminal = KOSDEC_ADX_INVALID;
            w->phase = FORMAT_BODY; w->need = 18;
        } else if(!memcmp(w->buffer, "SSND", 4)) {
            if(w->sound_ready || size < 8) return w->terminal = KOSDEC_ADX_INVALID;
            w->sound_bytes = size - 8; w->phase = SOUND_BODY; w->need = 8;
        } else w->offset = w->next_chunk;
    } else if(w->phase == FORMAT_BODY) {
        unsigned channels = be16(w->buffer), bits = be16(w->buffer + 6);
        if((channels != 1 && channels != 2) || (bits != 8 && bits != 16))
            return w->terminal = KOSDEC_ADX_UNSUPPORTED;
        uint32_t rate;
        status = sample_rate(w->buffer + 8, &rate);
        if(status != KOSDEC_ADX_MORE) return w->terminal = status;
        w->info.sample_rate = rate; w->info.channels = (uint8_t)channels;
        w->info.sample_bits = (uint8_t)bits; w->info.total_frames = be32(w->buffer + 2);
        w->frame_bytes = channels * (bits / 8); w->format_ready = true;
        w->offset = w->next_chunk; w->phase = CHUNK_HEADER; w->need = 8;
    } else {
        uint32_t padding = be32(w->buffer);
        if(padding > w->sound_bytes) return w->terminal = KOSDEC_ADX_INVALID;
        w->info.data_offset = w->offset + padding; w->sound_bytes -= padding;
        w->sound_ready = true;
        w->offset = w->next_chunk; w->phase = CHUNK_HEADER; w->need = 8;
    }
    return w->format_ready && w->sound_ready ? admit(w) : KOSDEC_ADX_MORE;
}
struct kosdec_adx_aiff_result kosdec_adx_aiff_read(struct kosdec_adx_aiff *w,
                                                 int16_t *out, size_t capacity) {
    struct kosdec_adx_aiff_result result = {KOSDEC_ADX_BAD_ARGUMENT, 0};
    if(!valid(w) || (!out && capacity) || (out && (uintptr_t)out % _Alignof(int16_t))) return result;
    if(w->terminal) { result.status = w->terminal;return result; }
    if(!capacity) { result.status = KOSDEC_ADX_NEED_OUTPUT;return result; }
    if(!w->header_ready) { result.status = header(w);return result; }
    if(w->used < w->frame_bytes) {
        uint32_t remaining = w->info.data_offset + w->info.data_bytes - w->offset;
        unsigned n = sizeof(w->buffer) - w->used;
        if(n > remaining) n = remaining;
        if(!n) { result.status = w->terminal = KOSDEC_ADX_TRUNCATED;return result; }
        result.status = fill(w, n);
        if(result.status != KOSDEC_ADX_MORE) return result;
    }
    size_t frames = w->used / w->frame_bytes;
    if(frames > capacity) frames = capacity;
    if(frames > 32) frames = 32;
    size_t samples = frames * w->info.channels;
    for(size_t i = 0; i < samples; ++i) {
        int value;
        if(w->info.sample_bits == 8) value = (w->buffer[i] < 128 ? (int)w->buffer[i] : (int)w->buffer[i] - 256) * 256;
        else { unsigned u = be16(w->buffer + i * 2);value = u < 32768 ? (int)u : (int)u - 65536; }
        out[i] = (int16_t)value;
    }
    if(frames) media_adx_filter_pcm(w->filter, w->filter_context, w->info.channels, out, frames);
    unsigned consumed = (unsigned)frames * w->frame_bytes;
    w->used -= consumed;
    if(w->used && consumed) memmove(w->buffer, w->buffer + consumed, w->used);
    w->frames_done += (uint32_t)frames;result.frames = frames;
    result.status = w->frames_done == w->info.total_frames ?
        (w->terminal = KOSDEC_ADX_DONE) : KOSDEC_ADX_MORE;
    return result;
}
enum kosdec_adx_status kosdec_adx_aiff_get_info(const struct kosdec_adx_aiff *w,
                                             struct kosdec_adx_aiff_info *out) {
    if(!valid(w) || !out) return KOSDEC_ADX_BAD_ARGUMENT;
    if(!w->header_ready) return w->terminal ? w->terminal : KOSDEC_ADX_NEED_INPUT;
    *out = w->info;return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_aiff_seek(struct kosdec_adx_aiff *w, uint32_t frame) {
    if(!valid(w) || !w->header_ready || frame > w->info.total_frames) return KOSDEC_ADX_BAD_ARGUMENT;
    w->offset = w->info.data_offset + frame * w->frame_bytes;w->frames_done = frame;w->used = 0;
    w->terminal = frame == w->info.total_frames ? KOSDEC_ADX_DONE : KOSDEC_ADX_MORE;
    return KOSDEC_ADX_DONE;
}
void kosdec_adx_aiff_cancel(struct kosdec_adx_aiff *w) {
    if(valid(w) && !w->terminal) w->terminal = KOSDEC_ADX_CANCELLED;
}
enum kosdec_adx_status kosdec_adx_aiff_set_filter(struct kosdec_adx_aiff *w,
    kosdec_adx_pcm_filter filter, void *context) {
    if(!valid(w)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(w->terminal) return w->terminal == KOSDEC_ADX_DONE ? KOSDEC_ADX_BAD_ARGUMENT : w->terminal;
    w->filter = filter; w->filter_context = filter ? context : NULL;
    return KOSDEC_ADX_DONE;
}
