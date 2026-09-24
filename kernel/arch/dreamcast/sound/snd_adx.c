/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Independently written bounded decoder. Public interoperability references
   and numerical policy are documented in doc/adx-decoder.md.
*/
#include <dc/sound/adx.h>
#include <math.h>
#include <string.h>

#define ADX_MAGIC UINT32_C(0x41445831)

static uint32_t be16(const uint8_t *p) {
    return (uint32_t)p[0] * 256u + p[1];
}

static uint32_t be32(const uint8_t *p) {
    return be16(p) * 65536u + be16(p + 2);
}

void snd_adx_init(snd_adx_decoder_t *d) {
    if(d) {
        memset(d, 0, sizeof(*d));
        d->magic = ADX_MAGIC;
        d->header_size = sizeof(d->header);
    }
}

void snd_adx_cancel(snd_adx_decoder_t *d) {
    if(d && d->magic == ADX_MAGIC)
        d->status = SND_ADX_CANCELLED;
}

static snd_adx_status_t parse_header(snd_adx_decoder_t *d) {
    const uint8_t *h = d->header;
    uint32_t cutoff = be16(h + 16);

    if(be16(h) != 0x8000 || be16(h + 2) < 22)
        return SND_ADX_INVALID;
    if(h[4] != 3 || h[5] != 18 || h[6] != 4 ||
       (h[7] != 1 && h[7] != 2) ||
       (h[18] != 3 && h[18] != 5) || h[19] != 0)
        return SND_ADX_UNSUPPORTED;

    d->info.sample_rate = be32(h + 8);
    d->info.total_frames = be32(h + 12);
    if(!d->info.sample_rate || !cutoff ||
       cutoff * 2u >= d->info.sample_rate ||
       d->info.total_frames > INT32_MAX)
        return SND_ADX_INVALID;
    if(d->info.sample_rate > 48000)
        return SND_ADX_UNSUPPORTED;

    d->info.channels = h[7];
    d->info.version = h[18];
    d->header_size = be16(h + 2) + 4u;
    return SND_ADX_MORE;
}

static void setup_filter(snd_adx_decoder_t *d) {
    /* Setup only: Q12 coefficients truncate toward zero. Keep intermediate
       float rounding explicit; never use approximate FPU math in prediction. */
    float angle_cos = cosf((float)(6.28318530717958647692 *
                                   be16(d->header + 16) / d->info.sample_rate));
    float a = (float)(1.41421356237309504880 - angle_cos);
    float b = (float)(1.41421356237309504880 - 1.0);
    float pole = (a - sqrtf((a + b) * (a - b))) / b;
    d->coefficient[0] = (int32_t)(pole * 8192.0f);
    d->coefficient[1] = (int32_t)(pole * pole * -4096.0f);
}

/* Arithmetic floor, including negative values, without implementation-defined
   signed shifts. Q12 coefficients and PCM16 histories keep products/sums well
   inside int32_t (absolute sum <= 12288 * 32768). */
static int32_t floor_q12(int32_t x) {
    if(x >= 0)
        return x / 4096;
    return -1 - (-(x + 1) / 4096);
}

static void decode_group(snd_adx_decoder_t *d, int16_t *out, size_t frames) {
    for(unsigned ch = 0; ch < d->info.channels; ++ch) {
        const uint8_t *block = d->block + ch * 18;
        int32_t scale = (int32_t)be16(block) + 1;
        for(size_t i = 0; i < frames; ++i) {
            unsigned shift = (i & 1) ? 0 : 4;
            int32_t residual = (block[2 + i / 2] >> shift) & 15;
            if(residual >= 8)
                residual -= 16;
            int32_t p0 = d->coefficient[0] * d->history[ch][0];
            int32_t p1 = d->coefficient[1] * d->history[ch][1];
            int32_t predicted = d->info.version == 3 ?
                floor_q12(p0) + floor_q12(p1) : floor_q12(p0 + p1);
            int32_t sample = residual * scale + predicted;
            if(sample > INT16_MAX)
                sample = INT16_MAX;
            if(sample < INT16_MIN)
                sample = INT16_MIN;
            d->history[ch][1] = d->history[ch][0];
            d->history[ch][0] = sample;
            out[i * d->info.channels + ch] = (int16_t)sample;
        }
    }
    d->frames_done += (uint32_t)frames;
    d->block_used = 0;
}

snd_adx_result_t snd_adx_decode(snd_adx_decoder_t *d,
                              const void *input, size_t input_bytes,
                              int16_t *output, size_t output_frames,
                              bool final_input) {
    snd_adx_result_t r = { SND_ADX_BAD_ARGUMENT, 0, 0 };
    const uint8_t *in = input;
    if(!d || d->magic != ADX_MAGIC || (!in && input_bytes) ||
       (!output && output_frames))
        return r;
    if(d->status >= SND_ADX_DONE) {
        r.status = d->status;
        return r;
    }

    if(!d->header_ready) {
        while(d->header_pos < d->header_size && r.consumed < input_bytes &&
              r.consumed < 256) {
            uint8_t byte = in[r.consumed++];
            if(d->header_pos < sizeof(d->header))
                d->header[d->header_pos] = byte;
            else if(d->header_pos >= d->header_size - sizeof(d->marker))
                d->marker[d->header_pos - (d->header_size - sizeof(d->marker))] = byte;
            ++d->header_pos;
            if(d->header_pos == sizeof(d->header)) {
                d->status = parse_header(d);
                if(d->status != SND_ADX_MORE) {
                    r.status = d->status;
                    return r;
                }
            }
        }
        if(d->header_pos == d->header_size) {
            if(memcmp(d->marker, "(c)CRI", sizeof(d->marker)))
                d->status = SND_ADX_INVALID;
            else {
                setup_filter(d);
                d->header_ready = true;
                if(!d->info.total_frames)
                    d->status = SND_ADX_DONE;
            }
            r.status = d->status;
        }
        else if(r.consumed == input_bytes) {
            r.status = final_input ? SND_ADX_TRUNCATED : SND_ADX_NEED_INPUT;
            if(final_input)
                d->status = r.status;
        }
        else
            r.status = SND_ADX_MORE;
        return r;
    }

    size_t group_bytes = 18u * d->info.channels;
    size_t frames = d->info.total_frames - d->frames_done;
    if(frames > 32)
        frames = 32;
    if(output_frames < frames) {
        r.status = SND_ADX_NEED_OUTPUT;
        return r;
    }

    size_t take = group_bytes - d->block_used;
    if(take > input_bytes)
        take = input_bytes;
    if(take)
        memcpy(d->block + d->block_used, in, take);
    d->block_used += take;
    r.consumed = take;
    if(d->block_used != group_bytes) {
        r.status = final_input ? SND_ADX_TRUNCATED : SND_ADX_NEED_INPUT;
        if(final_input)
            d->status = r.status;
        return r;
    }

    /* Validate both channel headers before modifying either history/output. */
    for(unsigned ch = 0; ch < d->info.channels; ++ch) {
        if(d->block[ch * 18] & 0x80) {
            d->status = r.status = SND_ADX_INVALID;
            return r;
        }
    }
    decode_group(d, output, frames);
    r.frames = frames;
    d->status = r.status = d->frames_done == d->info.total_frames ?
        SND_ADX_DONE : SND_ADX_MORE;
    return r;
}
