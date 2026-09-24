/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
/** \file dc/sound/adx_pipe.h
    \brief Bounded ADX producer / PCM consumer bridge.

    Exactly one producer (for example a decode fiber) and one consumer (an
    ordinary sound polling thread). No allocations, locks, waits or callbacks.
    Metadata and PCM are published using lock-free 32-bit acquire/release
    operations. Caller-owned storage must use ordinary coherent CPU memory;
    this bridge does not translate addresses, flush caches, or perform DMA.
*/
#ifndef __DC_SOUND_ADX_PIPE_H
#define __DC_SOUND_ADX_PIPE_H

#include <dc/sound/adx.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Caller-owned state; all fields are private to the implementation.
    Do not inspect counters directly, copy a live pipe, or reinitialize while
    any producer/consumer/control call is possible. */
typedef struct snd_adx_pipe {
    snd_adx_decoder_t decoder;
    int16_t *pcm;
    uint32_t capacity;
    uint32_t write_cursor, read_cursor;
    uint32_t ready, terminal, cancel_requested;
} snd_adx_pipe_t;

typedef struct snd_adx_pipe_result {
    size_t frames;               /**< PCM frames copied, not scalar samples. */
    size_t queued_frames;        /**< Remaining frames in the observed snapshot. */
    snd_adx_status_t producer;   /**< MORE while live; terminal codec status otherwise. */
    bool drained;               /**< Terminal producer and empty SOFTWARE ring. */
    bool starved;               /**< Requested PCM missing while producer was live. */
} snd_adx_pipe_result_t;

/** Initialize before sharing with any other thread.
    capacity_frames must be a power of two in [32, 1048576]. pcm_samples is the
    storage length in int16_t elements, and must be >= 2 * capacity_frames
    (maximum stereo). Mono uses only the first capacity_frames elements.
    Storage must be suitably aligned, disjoint from state/input/output and
    remain owned until all users have stopped. Returns 0, or -1 without
    modifying state on invalid arguments. No errno use. */
int snd_adx_pipe_init(snd_adx_pipe_t *pipe, int16_t *pcm,
                      size_t pcm_samples, size_t capacity_frames);

/** Producer only: one bounded codec step, decoding directly into the ring.
    Same input/final-input semantics as snd_adx_decode(). NEED_OUTPUT is ring
    backpressure: park the producer and arrange a wake after consumer progress.
    No input is consumed when the next PCM group does not fit. The caller must
    yield between successful bounded steps to maintain executor fairness.
    No direct pointer to ring storage is returned. */
snd_adx_result_t snd_adx_pipe_decode(snd_adx_pipe_t *pipe,
                                    const void *input, size_t input_bytes,
                                    bool final_input);

/** Consumer only: copy up to output_frames interleaved frames and release
    their ring slots after the copy. No silence is inserted. One call copies
    at most capacity_frames, in at most two spans. Output must not overlap the
    pipe, ring, or producer input. Keep callback scratch unchanged until KOS
    has consumed it; do not return the ring itself to a sound callback.

    A zero-capacity call (NULL output allowed) observes status without consuming
    data. drained is NOT an audio/DMA completion signal. After errors/cancel,
    already published PCM remains readable; the owner chooses drain or abort.
*/
snd_adx_pipe_result_t snd_adx_pipe_read(snd_adx_pipe_t *pipe,
                                       int16_t *output, size_t output_frames);

/** Any thread: read immutable format after full header validation. Returns
    true and copies info when ready; false leaves info unchanged. */
bool snd_adx_pipe_get_info(const snd_adx_pipe_t *pipe, snd_adx_info_t *info);

/** Any thread: request cancellation. Does not call the codec or discard PCM.
    Caller must also wake a parked producer. That producer acknowledges on its
    next decode step; existing terminal states are preserved. No function here
    stops AICA, joins a thread, or makes storage safe to free by itself. */
void snd_adx_pipe_request_cancel(snd_adx_pipe_t *pipe);

#ifdef __cplusplus
}
#endif
#endif
