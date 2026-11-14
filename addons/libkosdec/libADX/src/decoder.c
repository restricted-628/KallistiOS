/* Independent framing and lifetime policy for incremental ADX decoding. */
#include <kosdec/adx.h>
#include "adx_math.h"
#include <string.h>
#define ADX_MAGIC UINT32_C(0x41445846)
static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] * 256u + p[1]; }
static uint32_t be32(const uint8_t *p) { return be16(p) * 65536u + be16(p + 2); }
void kosdec_adx_init(struct kosdec_adx_decoder *d) {
    if(d) {
        memset(d, 0, sizeof(*d));
        d->magic = ADX_MAGIC;
        d->header_bytes = sizeof(d->header);
        d->footer_bytes = 4;
    }
}
void kosdec_adx_cancel(struct kosdec_adx_decoder *d) {
    if(d && d->magic == ADX_MAGIC && !d->terminal)
        d->terminal = KOSDEC_ADX_CANCELLED;
}
static enum kosdec_adx_status header(struct kosdec_adx_decoder *d) {
    const uint8_t *h = d->header;
    if(be16(h) != 0x8000) return KOSDEC_ADX_INVALID;
    d->header_bytes = be16(h + 2) + 4u;
    if(d->header_bytes < 26) return KOSDEC_ADX_INVALID;
    if(h[4] != 3 || h[5] != 18 || h[6] != 4 ||
       (h[7] != 1 && h[7] != 2) || (h[18] != 3 && h[18] != 5) || h[19] != 0)
        return KOSDEC_ADX_UNSUPPORTED;
    d->total_frames = be32(h + 12);
    if(d->total_frames > KOSDEC_ADX_MAX_FRAMES) return KOSDEC_ADX_UNSUPPORTED;
    /* One authority for certified profiles; metadata remains private until the
       complete header/extension passes and header_ready is published. */
    if(media_adx_coefficients(be32(h + 8), be16(h + 16), d->coefficient))
        return KOSDEC_ADX_UNSUPPORTED;
    d->sample_rate = be32(h + 8);
    d->cutoff = (uint16_t)be16(h + 16);
    d->channels = h[7];
    d->version = h[18];
    return KOSDEC_ADX_MORE;
}
static enum kosdec_adx_status extension(struct kosdec_adx_decoder *d) {
    const uint8_t *e = d->extension;
    unsigned nonzero = 0;
    for(unsigned i = 0; i < sizeof(d->extension); ++i) nonzero |= e[i];
    if(!nonzero) return KOSDEC_ADX_MORE;
    if(d->version != 3 || d->header_bytes < 50 || be16(e) >= 32 ||
       be16(e + 2) != 1 || be32(e + 4) != 1)
        return KOSDEC_ADX_UNSUPPORTED;
    uint32_t start = be32(e + 8), end = be32(e + 16);
    if(start >= end || end > d->total_frames) return KOSDEC_ADX_INVALID;
    if(be16(e) > start) return KOSDEC_ADX_INVALID;
    if(start % 32u) return KOSDEC_ADX_UNSUPPORTED;
    uint32_t group_bytes = 18u * d->channels;
    uint32_t start_byte = d->header_bytes + (start / 32u) * group_bytes;
    uint32_t end_byte = d->header_bytes + ((end + 31u) / 32u) * group_bytes;
    /* INT32_MAX frame admission bounds these calculations below UINT32_MAX. */
    if(be32(e + 12) != start_byte || be32(e + 20) != end_byte)
        return KOSDEC_ADX_INVALID;
    d->loop = (struct kosdec_adx_loop){true, start, end, start_byte, end_byte};
    return KOSDEC_ADX_MORE;
}
struct kosdec_adx_result kosdec_adx_decode(struct kosdec_adx_decoder *d,
    const uint8_t *input, size_t input_bytes, int16_t *output,
    size_t output_frames, bool final_input) {
    struct kosdec_adx_result r = {KOSDEC_ADX_BAD_ARGUMENT, 0, 0};
    if(!d || d->magic != ADX_MAGIC || (!input && input_bytes) ||
       (!output && output_frames) ||
       (output && (uintptr_t)output % _Alignof(int16_t))) return r;
    if(d->terminal) { r.status = d->terminal; return r; }
    if(!d->header_ready) {
        static const uint8_t marker[] = {'(', 'c', ')', 'C', 'R', 'I'};
        while(d->header_used < d->header_bytes && r.consumed < input_bytes &&
              r.consumed < KOSDEC_ADX_HEADER_BUDGET) {
            unsigned n = d->header_used;
            uint8_t value = input[r.consumed++];
            if(n < sizeof(d->header)) d->header[n] = value;
            else if(n < d->header_bytes - sizeof(marker)) {
                if(n < sizeof(d->header) + sizeof(d->extension))
                    d->extension[n - sizeof(d->header)] = value;
                else if(value) { r.status = KOSDEC_ADX_UNSUPPORTED; goto fail; }
            }
            else if(value != marker[n - (d->header_bytes - sizeof(marker))]) {
                r.status = KOSDEC_ADX_INVALID; goto fail;
            }
            ++d->header_used;
            if(d->header_used == sizeof(d->header)) {
                r.status = header(d);
                if(r.status != KOSDEC_ADX_MORE) goto fail;
            }
        }
        if(d->header_used != d->header_bytes) {
            if(r.consumed < input_bytes) {
                r.status = KOSDEC_ADX_MORE;
                return r;
            }
            r.status = final_input ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_NEED_INPUT;
            if(final_input) goto fail;
            return r;
        }
        r.status = extension(d);
        if(r.status != KOSDEC_ADX_MORE) goto fail;
        d->header_ready = true;
        r.status = KOSDEC_ADX_MORE;
        return r;
    }
    if(d->frames_done < d->total_frames) {
        size_t frames = d->total_frames - d->frames_done;
        if(frames > 32) frames = 32;
        if(output_frames < frames) { r.status = KOSDEC_ADX_NEED_OUTPUT; return r; }
        size_t group_bytes = 18u * d->channels;
        size_t take = group_bytes - d->block_used;
        if(take > input_bytes) take = input_bytes;
        if(take) memcpy(d->block + d->block_used, input, take);
        d->block_used += (uint8_t)take;
        r.consumed = take;
        if(d->block_used != group_bytes) {
            r.status = final_input ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_NEED_INPUT;
            if(final_input) goto fail;
            return r;
        }
        /* Both channels must be admitted before either history/output changes.
           Setup coefficients and typed histories satisfy the finite math domain;
           after this check neither channel can fail halfway through the group. */
        for(unsigned ch = 0; ch < d->channels; ++ch)
            if(d->block[ch * 18] & 0x80) {
                r.status = KOSDEC_ADX_INVALID; goto fail;
            }
        for(unsigned ch = 0; ch < d->channels; ++ch) {
            int clips = media_adx_channel_group(d->block + ch * 18, (unsigned)frames,
                        d->coefficient, d->history[ch], output + ch, d->channels);
            if(clips < 0) { r.status = KOSDEC_ADX_INVALID; goto fail; }
            d->clips += (uint32_t)clips;
        }
        d->frames_done += (uint32_t)frames;
        d->block_used = 0;
        r.frames = frames;
        r.status = KOSDEC_ADX_MORE;
        return r;
    }
    /* Supported end record: marker8001, payload length>=14, zero-filled payload.
       Footer validation is separate from declared sample completion. */
    while(d->footer_used < d->footer_bytes && r.consumed < input_bytes &&
          r.consumed < KOSDEC_ADX_HEADER_BUDGET) {
        unsigned n = d->footer_used;
        uint8_t value = input[r.consumed++];
        if(n == 2 || n == 3) d->footer_length[n - 2] = value;
        else if(value != (n == 0 ? 0x80 : n == 1 ? 1 : 0)) {
            r.status = KOSDEC_ADX_INVALID; goto fail;
        }
        ++d->footer_used;
        if(d->footer_used == 4) {
            d->footer_bytes = be16(d->footer_length) + 4u;
            if(d->footer_bytes < 18) { r.status = KOSDEC_ADX_UNSUPPORTED; goto fail; }
        }
    }
    if(d->footer_used != d->footer_bytes) {
        if(r.consumed < input_bytes) { r.status = KOSDEC_ADX_MORE; return r; }
        r.status = final_input ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_NEED_INPUT;
        if(final_input) goto fail;
        return r;
    }
    if(r.consumed != input_bytes) { r.status = KOSDEC_ADX_INVALID; goto fail; }
    r.status = final_input ? KOSDEC_ADX_DONE : KOSDEC_ADX_NEED_END;
    if(!final_input) return r;
fail:
    d->terminal = r.status;
    return r;
}

enum kosdec_adx_status kosdec_adx_get_info(const struct kosdec_adx_decoder *d,
                                        struct kosdec_adx_info *info) {
    if(!d || d->magic != ADX_MAGIC || !info) return KOSDEC_ADX_BAD_ARGUMENT;
    if(!d->header_ready) return d->terminal ? d->terminal : KOSDEC_ADX_NEED_INPUT;
    *info = (struct kosdec_adx_info){
        .header_bytes = d->header_bytes, .total_frames = d->total_frames,
        .sample_rate = d->sample_rate, .cutoff = d->cutoff,
        .inserted_frames = (uint16_t)be16(d->extension),
        .encoding = d->header[4], .block_bytes = d->header[5],
        .sample_bits = d->header[6], .channels = d->channels,
        .version = d->version, .loop = d->loop
    };
    return KOSDEC_ADX_DONE;
}

enum kosdec_adx_status kosdec_adx_probe_header(const uint8_t *input, size_t bytes,
                                            struct kosdec_adx_info *info) {
    if((!input && bytes) || !info) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_decoder d;
    kosdec_adx_init(&d);
    size_t used = 0;
    for(;;) {
        struct kosdec_adx_result r = kosdec_adx_decode(&d,
            input ? input + used : NULL, bytes - used, NULL, 0, false);
        used += r.consumed;
        if(d.header_ready) return kosdec_adx_get_info(&d, info);
        if(r.status != KOSDEC_ADX_MORE) return r.status;
    }
}

enum kosdec_adx_status kosdec_adx_probe_endcode(const uint8_t *input, size_t bytes,
                                             uint32_t *endcode_bytes) {
    if((!input && bytes) || !endcode_bytes) return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_decoder d;
    kosdec_adx_init(&d);
    /* This local parser has no header/body: only its existing footer state is
       exercised. The public decoder instance and its admission are untouched. */
    d.header_ready = true;
    size_t used = 0;
    for(;;) {
        size_t take = d.footer_bytes - d.footer_used;
        if(take > bytes - used) take = bytes - used;
        struct kosdec_adx_result r = kosdec_adx_decode(&d,
            input ? input + used : NULL, take, NULL, 0, false);
        used += r.consumed;
        if(r.status == KOSDEC_ADX_NEED_END) {
            *endcode_bytes = d.footer_bytes;
            return KOSDEC_ADX_DONE;
        }
        if(r.status != KOSDEC_ADX_MORE && r.status != KOSDEC_ADX_NEED_INPUT)
            return r.status;
        if(used == bytes) return KOSDEC_ADX_NEED_INPUT;
    }
}

enum kosdec_adx_status kosdec_adx_make_sfa_header(uint8_t *output, size_t capacity,
    unsigned channels, uint32_t sample_rate, uint32_t total_frames) {
    if(!output && capacity) return KOSDEC_ADX_BAD_ARGUMENT;
    if(channels != 1 && channels != 2) return KOSDEC_ADX_UNSUPPORTED;
    uint8_t header[KOSDEC_ADX_SFA_HEADER_BYTES] = {0};
    header[0] = 0x80;
    header[2] = (KOSDEC_ADX_SFA_HEADER_BYTES - 4) >> 8;
    header[3] = (uint8_t)(KOSDEC_ADX_SFA_HEADER_BYTES - 4);
    header[4] = 3; header[5] = 18; header[6] = 4;
    header[7] = (uint8_t)channels;
    for(unsigned i = 0; i < 4; ++i) {
        header[8 + i] = (uint8_t)(sample_rate >> (24 - i * 8));
        header[12 + i] = (uint8_t)(total_frames >> (24 - i * 8));
    }
    header[16] = 1; header[17] = 0xf4; header[18] = 3;
    memcpy(header + sizeof(header) - 6, "(c)CRI", 6);
    struct kosdec_adx_info info;
    enum kosdec_adx_status status = kosdec_adx_probe_header(header, sizeof(header), &info);
    if(status != KOSDEC_ADX_DONE) return status;
    if(capacity < sizeof(header)) return KOSDEC_ADX_NEED_OUTPUT;
    memcpy(output, header, sizeof(header));
    return KOSDEC_ADX_DONE;
}
