/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
/** \file dc/sound/adx.h
    \brief Bounded, allocation-free ADX memory decoder (experimental).

    No device access, threads, callbacks, address aliases, or implicit looping.
    See doc/adx-decoder.md for the deliberately limited supported profile.
*/
#ifndef __DC_SOUND_ADX_H
#define __DC_SOUND_ADX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum snd_adx_status {
    SND_ADX_MORE = 0,       /**< Work budget reached; call again. */
    SND_ADX_NEED_INPUT,     /**< Supply more bytes. */
    SND_ADX_NEED_OUTPUT,    /**< Supply space for one block group. */
    SND_ADX_DONE,           /**< Declared samples delivered; trailer unconsumed. */
    SND_ADX_INVALID,        /**< Malformed data or premature EOF block. */
    SND_ADX_UNSUPPORTED,    /**< Profile outside the supported subset. */
    SND_ADX_TRUNCATED,      /**< Final input ended before declared samples. */
    SND_ADX_CANCELLED,
    SND_ADX_BAD_ARGUMENT
} snd_adx_status_t;

typedef struct snd_adx_info {
    uint32_t sample_rate;
    uint32_t total_frames;  /**< PCM frames (samples per channel), not bytes. */
    uint8_t channels;
    uint8_t version;
} snd_adx_info_t;

/** Caller-owned state. Initialize before use; do not modify fields afterward.
    info is readable only after header_ready becomes true. One owner at a time;
    distinct states may be freely interleaved. Copying does not allocate memory.
*/
typedef struct snd_adx_decoder {
    snd_adx_info_t info;
    uint32_t frames_done;
    bool header_ready;
    /* Implementation state; not a stable serialized representation. */
    uint32_t magic;
    uint32_t header_pos, header_size;
    int32_t coefficient[2];
    int32_t history[2][2];
    size_t block_used;
    snd_adx_status_t status;
    uint8_t header[20], marker[6], block[36];
} snd_adx_decoder_t;

typedef struct snd_adx_result {
    snd_adx_status_t status;
    size_t consumed;       /**< Input bytes consumed in this call. */
    size_t frames;         /**< Interleaved PCM frames written in this call. */
} snd_adx_result_t;

/** Initialize/restart an instance. NULL is ignored. */
void snd_adx_init(snd_adx_decoder_t *decoder);

/** Decode at most 32 PCM frames, or inspect at most 256 header bytes per call.

    Input may arrive in arbitrary pieces. Always advance by result.consumed.
    Output is native-endian signed PCM16, interleaved; output_frames is frame
    capacity, so storage must hold output_frames * channels int16_t values.
    A block requires min(32, remaining frames) capacity; smaller output returns
    NEED_OUTPUT without decoding. At most 36 compressed bytes are retained.

    final_input means this span ends the input stream. Reassert it on subsequent
    calls draining that final span/state. Truncation is reported only when more
    input is needed; an output stall is not truncation. DONE uses the declared
    frame count, does not consume/validate a trailing EOF record, and is sticky.
    Format errors and cancellation are sticky until init. BAD_ARGUMENT does
    not change state. NULL input/output is allowed only with zero size/capacity.

    Decoder, input, output, and other live instances must not overlap. Caller
    owns bounds/lifetimes. Cancel between calls, not concurrently. The codec
    does not yield, block, allocate, or perform any CPU-address translation.
*/
snd_adx_result_t snd_adx_decode(snd_adx_decoder_t *decoder,
                              const void *input, size_t input_bytes,
                              int16_t *output, size_t output_frames,
                              bool final_input);

/** Stop an initialized instance between decode calls. NULL is ignored. */
void snd_adx_cancel(snd_adx_decoder_t *decoder);

#ifdef __cplusplus
}
#endif
#endif
