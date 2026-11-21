#include "adx_container_internal.h"
#include <string.h>
#include "adx_filter.h"
#define WAVE_MAGIC UINT32_C(0x41445857)
enum { RIFF_HEADER, CHUNK_HEADER, FORMAT_BODY, PCM_BODY };
static bool valid(const struct kosdec_adx_wave *w) { return w && w->magic == WAVE_MAGIC; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
enum kosdec_adx_status kosdec_adx_wave_init(struct kosdec_adx_wave *w,
    kosdec_adx_read_at read_at, void *context, uint32_t bytes) {
    if(!w || !read_at) return KOSDEC_ADX_BAD_ARGUMENT;
    memset(w, 0, sizeof(*w));w->magic = WAVE_MAGIC;w->read_at = read_at;w->context = context;
    w->input_bytes = bytes;w->need = 12;
    return w->terminal = bytes < 12 ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_MORE;
}
static enum kosdec_adx_status fill(struct kosdec_adx_wave *w, unsigned n) {
    enum kosdec_adx_status status =
        media_adx_container_fill(w->read_at, w->context, w->buffer, &w->used, &w->offset, n);
    if(status >= KOSDEC_ADX_DONE)
        w->terminal = status;
    return status;
}
static unsigned pcm_request(const struct kosdec_adx_wave *w) {
    uint32_t remaining = w->info.data_offset + w->info.data_bytes - w->offset;
    return media_adx_container_pcm_request(MEDIA_ADX_CONTAINER_CHUNK - w->used, remaining);
}

enum kosdec_adx_status media_adx_wave_step_file(struct kosdec_adx_wave *w) {
    if(!valid(w)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(w->terminal) return w->terminal;
    if(!w->header_ready) {
        if(w->phase == CHUNK_HEADER && !w->used &&
           (w->offset > w->riff_end || w->riff_end - w->offset < 8))
            return KOSDEC_ADX_MORE;
        unsigned n = w->need - w->used;
        return n ? fill(w, n) : KOSDEC_ADX_MORE;
    }

    enum kosdec_adx_status status = w->staged_error;
    if(!status) {
        uint32_t end = w->info.data_offset + w->info.data_bytes;
        if(w->offset == end)
            return KOSDEC_ADX_MORE;
        media_adx_container_compact(w->buffer, &w->used, &w->input_pos);
        status = media_adx_container_stage(w->read_at, w->context, w->buffer, sizeof(w->buffer),
                                           &w->used, &w->offset, end);
        if(status < KOSDEC_ADX_DONE)
            return status;
        w->staged_error = status;
    }
    /* Keep the complete prefix available to DECODER before publishing the
       failure. An incomplete final unit cannot be reconstructed. */
    if(w->used - w->input_pos >= w->frame_bytes || w->cached_frames)
        return KOSDEC_ADX_MORE;
    return w->terminal = status;
}

static enum kosdec_adx_status header(struct kosdec_adx_wave *w, bool staged) {
    if(w->phase == CHUNK_HEADER && !w->used &&
       (w->offset > w->riff_end || w->riff_end - w->offset < 8))
        return w->terminal = KOSDEC_ADX_INVALID;
    if(w->used < w->need) {
        if(staged) return KOSDEC_ADX_NEED_INPUT;
        enum kosdec_adx_status status = fill(w, w->need - w->used);
        if(status != KOSDEC_ADX_MORE || w->used < w->need) return status;
    }
    w->used = 0;
    if(w->phase == RIFF_HEADER) {
        if(memcmp(w->buffer, "RIFF", 4) || memcmp(w->buffer + 8, "WAVE", 4))
            return w->terminal = KOSDEC_ADX_UNSUPPORTED;
        uint32_t size = le32(w->buffer + 4);
        if(size < 4 || size > w->input_bytes - 8) return w->terminal = KOSDEC_ADX_INVALID;
        w->riff_end = size + 8;w->phase = CHUNK_HEADER;w->need = 8;
    } else if(w->phase == CHUNK_HEADER) {
        uint32_t size = le32(w->buffer + 4), remaining = w->riff_end - w->offset;
        if(size > remaining || (size & 1u) > remaining - size)
            return w->terminal = KOSDEC_ADX_INVALID;
        w->next_chunk = w->offset + size + (size & 1u);
        if(!memcmp(w->buffer, "fmt ", 4)) {
            if(w->format_ready || size < 16) return w->terminal = KOSDEC_ADX_INVALID;
            w->phase = FORMAT_BODY;w->need = 16;
        } else if(!memcmp(w->buffer, "data", 4)) {
            if(!w->format_ready || size % w->frame_bytes) return w->terminal = KOSDEC_ADX_INVALID;
            w->info.data_offset = w->offset;w->info.data_bytes = size;
            if(w->info.sample_bits == 4 && size / w->frame_bytes > UINT32_MAX / 4)
                return w->terminal = KOSDEC_ADX_INVALID;
            w->info.total_frames = size / w->frame_bytes * (w->info.sample_bits == 4 ? 4u : 1u);
            w->header_ready = true;w->phase = PCM_BODY;
            if(!size) return w->terminal = KOSDEC_ADX_DONE;
        } else w->offset = w->next_chunk;
    } else {
        unsigned tag = le16(w->buffer), channels = le16(w->buffer + 2), bits = le16(w->buffer + 14);
        uint32_t rate = le32(w->buffer + 4);
        if(tag != 1 || (channels != 1 && channels != 2) || (bits != 4 && bits != 8 && bits != 16))
            return w->terminal = KOSDEC_ADX_UNSUPPORTED;
        unsigned align = channels * (bits == 4 ? 2 : bits / 8);
        if(!rate || (bits != 4 && (le16(w->buffer + 12) != align ||
           (uint64_t)rate * align != le32(w->buffer + 8)))) return w->terminal = KOSDEC_ADX_INVALID;
        w->info.sample_rate = rate;w->info.channels = (uint8_t)channels;w->info.sample_bits = (uint8_t)bits;
        w->frame_bytes = align;w->format_ready = true;
        w->step[0] = w->step[1] = 127;
        w->offset = w->next_chunk;w->phase = CHUNK_HEADER;w->need = 8;
    }
    return KOSDEC_ADX_MORE;
}
/* AICA reconstruction: independent integer evaluation of the documented
   predictor and quantizer, including its intermediate magnitude saturation. */
static int16_t reconstruct(struct kosdec_adx_wave *w, unsigned channel, unsigned code) {
    static const unsigned factor[8] = {230,230,230,230,307,409,512,614};
    unsigned magnitude = code & 7u;
    int delta = w->step[channel] * (int)(2 * magnitude + 1) / 8;
    if(delta > 32767) delta = 32767;
    int sample = w->previous[channel] + (code & 8u ? -delta : delta);
    if(sample > 32767) sample = 32767;
    if(sample < -32768) sample = -32768;
    int quantizer = (int)((unsigned)w->step[channel] * factor[magnitude] / 256);
    if(quantizer < 127) quantizer = 127;
    if(quantizer > 24576) quantizer = 24576;
    w->previous[channel] = sample; w->step[channel] = quantizer;
    return (int16_t)sample;
}
static struct kosdec_adx_wave_result packed_read(struct kosdec_adx_wave *w,
                                                int16_t *out, size_t capacity, bool staged) {
    struct kosdec_adx_wave_result r = {KOSDEC_ADX_MORE, 0};
    bool discard = w->seeking, called = false;
    size_t goal = discard ? w->seek_target - w->frames_done : capacity;
    if(goal > 32) goal = 32;
    size_t processed = 0;
    unsigned channels = w->info.channels;
    while(processed < goal) {
        if(!w->cached_frames) {
            if(w->used - w->input_pos < w->frame_bytes) {
                if(called) break;
                if(w->staged_error) {
                    r.status = w->terminal = w->staged_error;
                    break;
                }
                if(staged) { r.status = KOSDEC_ADX_NEED_INPUT; break; }
                media_adx_container_compact(w->buffer, &w->used, &w->input_pos);
                unsigned n = pcm_request(w);
                if(!n) { r.status = w->terminal = KOSDEC_ADX_TRUNCATED; break; }
                called = true; r.status = fill(w, n);
                if(r.status != KOSDEC_ADX_MORE) break;
                if(w->used < w->frame_bytes) break;
            }
            for(unsigned f = 0; f < 4; ++f) for(unsigned ch = 0; ch < channels; ++ch) {
                unsigned byte = w->buffer[w->input_pos + (f / 2) * channels + ch];
                unsigned code = (byte >> (4 * (f & 1u))) & 15u;
                w->cached[f * channels + ch] = reconstruct(w, ch, code);
            }
            w->input_pos += w->frame_bytes; w->cached_pos = 0; w->cached_frames = 4;
        }
        size_t n = goal - processed;
        if(n > w->cached_frames) n = w->cached_frames;
        if(!discard) memcpy(out + processed * channels, w->cached + w->cached_pos * channels,
                            n * channels * sizeof(*out));
        w->cached_pos += (unsigned)n; w->cached_frames -= (unsigned)n;
        processed += n; w->frames_done += (uint32_t)n;
        if(w->frames_done == w->info.total_frames) { r.status = w->terminal = KOSDEC_ADX_DONE; break; }
    }
    if(discard) {
        if(w->frames_done == w->seek_target) w->seeking = false;
    } else {
        r.frames = processed;
        if(processed) media_adx_filter_pcm(w->filter, w->filter_context, channels, out, processed);
    }
    return r;
}
struct kosdec_adx_wave_result media_adx_wave_read(struct kosdec_adx_wave *w,
                                                 int16_t *out, size_t capacity, bool staged) {
    struct kosdec_adx_wave_result result = {KOSDEC_ADX_BAD_ARGUMENT, 0};
    if(!valid(w) || (!out && capacity) || (out && (uintptr_t)out % _Alignof(int16_t))) return result;
    if(w->terminal) { result.status = w->terminal;return result; }
    if(!capacity) { result.status = KOSDEC_ADX_NEED_OUTPUT;return result; }
    if(!w->header_ready) { result.status = header(w, staged);return result; }
    if(w->info.sample_bits == 4) return packed_read(w, out, capacity, staged);
    if(w->used - w->input_pos < w->frame_bytes) {
        if(w->staged_error) {
            result.status = w->terminal = w->staged_error;
            return result;
        }
        if(staged) { result.status = KOSDEC_ADX_NEED_INPUT; return result; }
        media_adx_container_compact(w->buffer, &w->used, &w->input_pos);
        unsigned n = pcm_request(w);
        if(!n) { result.status = w->terminal = KOSDEC_ADX_TRUNCATED;return result; }
        result.status = fill(w, n);
        if(result.status != KOSDEC_ADX_MORE) return result;
    }
    size_t frames = (w->used - w->input_pos) / w->frame_bytes;
    if(frames > capacity) frames = capacity;
    if(frames > 32) frames = 32;
    size_t samples = frames * w->info.channels;
    const uint8_t *input = w->buffer + w->input_pos;
    for(size_t i = 0; i < samples; ++i) {
        int value;
        if(w->info.sample_bits == 8) value = ((int)input[i] - 128) * 256;
        else { unsigned u = le16(input + i * 2);value = u < 32768 ? (int)u : (int)u - 65536; }
        out[i] = (int16_t)value;
    }
    if(frames) media_adx_filter_pcm(w->filter, w->filter_context, w->info.channels, out, frames);
    w->input_pos += (unsigned)frames * w->frame_bytes;
    w->frames_done += (uint32_t)frames;result.frames = frames;
    result.status = w->frames_done == w->info.total_frames ?
        (w->terminal = KOSDEC_ADX_DONE) : KOSDEC_ADX_MORE;
    return result;
}
struct kosdec_adx_wave_result kosdec_adx_wave_read(struct kosdec_adx_wave *w,
                                               int16_t *out, size_t capacity) {
    return media_adx_wave_read(w, out, capacity, false);
}
enum kosdec_adx_status kosdec_adx_wave_get_info(const struct kosdec_adx_wave *w,
                                             struct kosdec_adx_wave_info *out) {
    if(!valid(w) || !out) return KOSDEC_ADX_BAD_ARGUMENT;
    if(!w->header_ready) return w->terminal ? w->terminal : KOSDEC_ADX_NEED_INPUT;
    *out = w->info;return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_wave_seek(struct kosdec_adx_wave *w, uint32_t frame) {
    if(!valid(w) || !w->header_ready || frame > w->info.total_frames) return KOSDEC_ADX_BAD_ARGUMENT;
    w->used = w->input_pos = w->cached_pos = w->cached_frames = 0;
    w->seeking = false;
    w->staged_error = KOSDEC_ADX_MORE;
    w->terminal = frame == w->info.total_frames ? KOSDEC_ADX_DONE : KOSDEC_ADX_MORE;
    if(w->info.sample_bits == 4) {
        w->previous[0] = w->previous[1] = 0; w->step[0] = w->step[1] = 127;
        w->offset = w->info.data_offset; w->frames_done = frame == w->info.total_frames ? frame : 0;
        w->seek_target = frame; w->seeking = frame && frame < w->info.total_frames;
    } else {
        w->offset = w->info.data_offset + frame * w->frame_bytes; w->frames_done = frame;
    }
    return w->seeking ? KOSDEC_ADX_MORE : KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_wave_seek_step(struct kosdec_adx_wave *w) {
    if(!valid(w) || !w->header_ready) return KOSDEC_ADX_BAD_ARGUMENT;
    if(w->terminal >= KOSDEC_ADX_INVALID) return w->terminal;
    if(!w->seeking) return KOSDEC_ADX_DONE;
    int16_t unused[64];
    struct kosdec_adx_wave_result r = kosdec_adx_wave_read(w, unused, 32);
    return r.status >= KOSDEC_ADX_DONE ? r.status : w->seeking ? r.status : KOSDEC_ADX_DONE;
}
void kosdec_adx_wave_cancel(struct kosdec_adx_wave *w) {
    if(valid(w) && !w->terminal) w->terminal = KOSDEC_ADX_CANCELLED;
}
enum kosdec_adx_status kosdec_adx_wave_set_filter(struct kosdec_adx_wave *w,
    kosdec_adx_pcm_filter filter, void *context) {
    if(!valid(w)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(w->terminal) return w->terminal == KOSDEC_ADX_DONE ? KOSDEC_ADX_BAD_ARGUMENT : w->terminal;
    w->filter = filter; w->filter_context = filter ? context : NULL;
    return KOSDEC_ADX_DONE;
}
