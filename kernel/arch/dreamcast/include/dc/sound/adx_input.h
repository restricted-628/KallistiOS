/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
/** \file dc/sound/adx_input.h
    \brief Bounded byte input for the ADX fiber/PCM bridge.

    One loader writes/closes; one decoder service steps. The loader may perform
    I/O on its own ordinary thread, but none of these APIs performs I/O or waits.
    All storage is borrowed, disjoint and valid until both owners quiesce.
*/
#ifndef __DC_SOUND_ADX_INPUT_H
#define __DC_SOUND_ADX_INPUT_H
#include <dc/sound/adx_pipe.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum snd_adx_input_end {
    SND_ADX_INPUT_OPEN = 0,
    SND_ADX_INPUT_EOF,
    SND_ADX_INPUT_FAILED
} snd_adx_input_end_t;

/** Private fields; do not copy/reset a live object or inspect cursors directly. */
typedef struct snd_adx_input {
    uint8_t *bytes;
    uint32_t capacity, write_cursor, read_cursor, end;
} snd_adx_input_t;

typedef struct snd_adx_input_result {
    snd_adx_result_t codec;      /**< One bounded PCM-pipe step. */
    snd_adx_input_end_t input;   /**< Observed source state; separate from codec EOF. */
} snd_adx_input_result_t;

/** Initialize before sharing; power-of-two byte capacity in [32, 1048576].
    Returns 0 or -1 for invalid arguments without changing state. No errno. */
int snd_adx_input_init(snd_adx_input_t *input, uint8_t *storage, size_t capacity);

/** Loader only: copy as much as fits and return accepted bytes. A partial write
    is backpressure, not EOF: retain the unaccepted suffix until a later call.
    Copies at most capacity bytes, in at most two spans. Returns 0 for full,
    closed or invalid arguments. No error/EOF is inferred from a zero write.
    Wake the decoder after publishing bytes. Source must not overlap storage.
*/
size_t snd_adx_input_write(snd_adx_input_t *input, const void *bytes, size_t size);

/** Loader only: publish EOF only after every successful read byte is accepted.
    Publish FAILED for a source I/O error, not EOF. Terminal state is immutable;
    repeating the same close succeeds, changing it fails. Wake the decoder
    after closing, including an empty input. Returns 0 or -1; no errno.
*/
int snd_adx_input_close(snd_adx_input_t *input, snd_adx_input_end_t end);

/** Any thread: read source state (NULL returns FAILED). Does not report codec
    completion or whether any bytes remain buffered. */
snd_adx_input_end_t snd_adx_input_get_end(const snd_adx_input_t *input);

/** Decoder service only: pass one contiguous input span directly to the PCM
    pipe, then release only the bytes actually consumed. Bound remains one
    codec step (256 header bytes or 32 PCM frames). NEED_INPUT and NEED_OUTPUT
    require separate input-ready/output-space wakes; do not busy-spin.

    On source FAILED, cancel the pipe rather than treating the stream as a
    successful EOF. result.input preserves the source-failure distinction;
    previously published PCM remains available. A preexisting terminal codec
    state is not overwritten. Source error details belong to the loader.
    EOF across a ring wrap is final only on the last physical span. Codec DONE
    can leave a trailer in the input queue: stop/join the loader before reclaiming
    or resetting either queue. Caller must wake a loader parked for free space
    after codec.consumed > 0. No callbacks are invoked inside this function.
*/
snd_adx_input_result_t snd_adx_input_step(snd_adx_input_t *input,
                                         snd_adx_pipe_t *pipe);
#ifdef __cplusplus
}
#endif
#endif
