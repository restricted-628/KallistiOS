/* KallistiOS ##version##

   dc/sound/stream.h
   Copyright (C) 2002, 2004 Megan Potter
   Copyright (C) 2020 Lawrence Sebald
   Copyright (C) 2023, 2024 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/sound/stream.h
    \brief   Sound streaming support.
    \ingroup audio_streaming

    This file contains declarations for doing streams of sound. This underlies
    pretty much any decoded sounds you might use, including the Ogg Vorbis
    libraries. Note that this does not actually handle decoding, so you'll have
    to worry about that yourself (or use something in kos-ports).

    \author Megan Potter
    \author Florian Schulze
    \author Lawrence Sebald
    \author Ruslan Rostovtsev
*/

#ifndef __DC_SOUND_STREAM_H
#define __DC_SOUND_STREAM_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <dc/sound/sound.h>

/** \defgroup audio_streaming   Streaming
    \brief                      Streaming audio playback and management
    \ingroup                    audio
    @{
*/

/** \brief  The maximum number of streams that can be allocated at once. */
#define SND_STREAM_MAX 4

/** \brief  Maximum 64-byte-aligned buffer for a 16-bit PCM channel.

    This is the largest aligned buffer whose sample count fits the AICA's
    16-bit loop-end register.
*/
#define SND_STREAM_BUFFER_MAX_PCM16 131008

/** \brief  Maximum 64-byte-aligned buffer for an 8-bit PCM channel. */
#define SND_STREAM_BUFFER_MAX_PCM8  65472

/** \brief  The maximum buffer size for each channel of ADPCM stream. */
#define SND_STREAM_BUFFER_MAX_ADPCM ((32 << 10) - 64)

/** \brief  The maximum buffer size for each channel of streams by default
            and for backward compatibility. */
#define SND_STREAM_BUFFER_MAX       (64 << 10)

/** \brief  Stream handle type.

    Each stream will be assigned a handle, which will be of this type. Further
    operations on the stream will use the handle to identify which stream is
    being referred to.
*/
typedef int snd_stream_hnd_t;

/** \brief  Invalid stream handle.

    If a stream cannot be allocated, this will be returned.
*/
#define SND_STREAM_INVALID -1

/** \brief Default bound for one stream-buffer transfer, in milliseconds. */
#define SND_STREAM_TRANSFER_TIMEOUT_DEFAULT 1000u

/** \brief Checked stream sample formats. */
typedef enum snd_stream_sample_format {
    SND_STREAM_FORMAT_PCM16 = SND_CHANNEL_SAMPLE_PCM16,
    SND_STREAM_FORMAT_PCM8 = SND_CHANNEL_SAMPLE_PCM8,
    SND_STREAM_FORMAT_ADPCM = SND_CHANNEL_SAMPLE_ADPCM_LOOP
} snd_stream_sample_format_t;

/** \brief Live controls for one physical channel of a logical stream. */
typedef struct snd_stream_channel_config {
    uint8_t volume;                 /**< Linear volume, 0 through 255. */
    uint8_t pan;                    /**< 0 left, 128 center, 255 right. */
    snd_channel_envelope_t envelope;/**< Amplitude envelope. */
    snd_channel_lfo_t lfo;          /**< Pitch and amplitude LFOs. */
    snd_channel_routing_t routing;  /**< Direct and DSP routing. */
    snd_channel_filter_t filter;    /**< Time-variant filter. */
} snd_stream_channel_config_t;

/** \brief Complete checked configuration for one logical stream. */
typedef struct snd_stream_config {
    snd_stream_sample_format_t format; /**< Stream sample encoding. */
    uint32_t sample_rate;              /**< Playback frequency in Hz. */
    uint8_t channels;                  /**< One or two physical channels. */
    uint32_t transfer_timeout_ms;      /**< Nonzero buffer-transfer bound. */
    snd_stream_channel_config_t channel[2]; /**< Per-channel live controls. */
} snd_stream_config_t;

/** \brief Stream lifecycle state. */
typedef enum snd_stream_state {
    SND_STREAM_STATE_ALLOCATED = 0, /**< Resources owned; never started. */
    SND_STREAM_STATE_QUEUED,        /**< Prefilled and waiting for queue-go. */
    SND_STREAM_STATE_PLAYING,       /**< Playback is active. */
    SND_STREAM_STATE_UNDERRUN,      /**< Silence substituted for missing data. */
    SND_STREAM_STATE_STOPPED,       /**< Playback was stopped cleanly. */
    SND_STREAM_STATE_ERROR          /**< Last lifecycle operation failed. */
} snd_stream_state_t;

/** \brief Coherent logical-stream status. */
typedef struct snd_stream_status {
    snd_stream_state_t state;       /**< Current lifecycle state. */
    int last_error;                 /**< Last errno value, or zero. */
    snd_stream_sample_format_t format; /**< Active encoding. */
    uint32_t sample_rate;           /**< Active playback frequency. */
    uint8_t channels;               /**< Active physical-channel count. */
    bool queueing;                  /**< Whether starts remain queued. */
    bool dma_pending;               /**< Whether a buffer DMA is active. */
    size_t buffer_size;             /**< Active bytes per channel. */
    uint32_t play_position;         /**< Channel-zero position in sample frames. */
    uint32_t write_position;        /**< Next write position in sample frames. */
    uint64_t source_bytes;          /**< Interleaved bytes accepted from callbacks. */
    uint64_t buffered_bytes;        /**< Interleaved bytes written, including silence. */
    uint64_t played_bytes;          /**< Estimated interleaved bytes consumed. */
    uint32_t polls;                 /**< Poll calls after the latest start. */
    uint32_t underruns;             /**< Polls that substituted silence. */
    int channel[2];                 /**< Owned AICA channel numbers. */
    bool channel_playing[2];        /**< Coherent per-channel playback state. */
} snd_stream_status_t;

/** \brief Initialize a checked stream configuration with safe defaults.

    The default is one-channel 16-bit PCM at 44.1 kHz, centered at full
    volume, with direct output enabled and a one-second transfer deadline.

    \param config          Configuration to initialize.
    \retval 0              On success.
    \retval -1             On invalid input with errno set to EINVAL.
*/
int snd_stream_config_init(snd_stream_config_t *config);

/** \brief Validate a checked stream configuration.

    \param config          Configuration to validate.
    \retval 0              When every field is valid.
    \retval -1             On invalid input with errno set to EINVAL.
*/
int snd_stream_config_validate(const snd_stream_config_t *config);

/** \brief  Stream get data callback type.

    Functions for providing stream data will be of this type, and can be
    registered with snd_stream_set_callback().

    \param  hnd             The stream handle being referred to.
    \param  smp_req         The number of interleaved bytes requested.
    \param  smp_recv        Used to return the number of interleaved bytes
                            available. This must be nonnegative, no larger than
                            smp_req, and end on a complete sample-frame
                            boundary. Short results are padded with silence.
    \return                 A pointer to the buffer of samples. If stereo, the
                            samples should be interleaved. For best performance
                            use a 32-byte aligned pointer. The stream copies the
                            returned bytes before the enclosing start or poll
                            call returns and before invoking this callback
                            again. Keep the buffer unchanged until then.
*/
typedef void *(*snd_stream_callback_t)(snd_stream_hnd_t hnd, int smp_req,
                                       int *smp_recv);

/** \brief  Direct stream data transfer callback type.

    Functions for providing stream data will be of this type, and can be
    registered with snd_stream_set_callback_direct().

    \param  hnd             The stream handle being referred to.
    \param  left            Left channel buffer address on AICA side.
    \param  right           Right channel buffer address on AICA side.
    \param  size_req        Requested interleaved byte count. For stereo, each
                            channel destination receives half this amount.
    \retval SIZE_MAX        On failure with errno set.
    \retval size_recv       On success. This must not exceed size_req and must
                            end on a complete sample-frame boundary. Short
                            results are padded with silence in sound RAM.
*/
typedef size_t (*snd_stream_callback_direct_t)(snd_stream_hnd_t hnd,
    uintptr_t left,  uintptr_t right,  size_t size_req);

/** \brief  Set the callback for a given stream.

    This function sets the get data callback function for a given stream,
    overwriting any old callback that may have been in place.

    \param  hnd             The stream handle for the callback.
    \param  cb              A pointer to the callback function.
*/
void snd_stream_set_callback(snd_stream_hnd_t hnd, snd_stream_callback_t cb);

/** \brief  Set the callback for a given stream with direct transfer.

    This function sets the get data callback function for a given stream,
    overwriting any old callback that may have been in place.

    \param  hnd             The stream handle for the callback.
    \param  cb              A pointer to the callback function.
*/
void snd_stream_set_callback_direct(snd_stream_hnd_t hnd, snd_stream_callback_direct_t cb);

/** \brief  Set the user data for a given stream.

    This function sets the user data pointer for the given stream, overwriting
    any existing one that may have been in place. This is designed to allow the
    user the ability to associate a piece of data with the stream for instance
    to assist in identifying what sound is playing on a stream. The driver does
    not attempt to use this data in any way.

    \param  hnd             The stream handle to look up.
    \param  d               A pointer to the user data.
*/
void snd_stream_set_userdata(snd_stream_hnd_t hnd, void *d);

/** \brief  Get the user data for a given stream.

    This function retrieves the set user data pointer for a given stream.

    \param  hnd             The stream handle to look up.
    \return                 The user data pointer set for this stream or NULL
                            if no data pointer has been set.
*/
void *snd_stream_get_userdata(snd_stream_hnd_t hnd);

/** \brief  Stream filter callback type.

    Functions providing filters over the stream data will be of this type, and
    can be set with snd_stream_filter_add().

    \param  hnd             The stream being referred to.
    \param  obj             Filter user data.
    \param  hz              The frequency of the sound data.
    \param  channels        The number of channels in the sound data.
    \param  buffer          A pointer to the buffer to process. This is before
                            any stereo separation is done. Can be changed by the
                            filter, if appropriate.
    \param  samplecnt       A pointer to the number of samples. This can be
                            modified by the filter, if appropriate.
*/
typedef void (*snd_stream_filter_t)(snd_stream_hnd_t hnd, void *obj, int hz,
                                    int channels, void **buffer,
                                    int *samplecnt);

/** \brief  Add a filter to the specified stream.

    This function adds a filter to the specified stream. The filter will be
    called on each block of data input to the stream from then forward.

    When the stream buffer filler needs more data, it starts out by calling
    the initial callback (set above). It then calls each function in the
    effect filter chain, which can modify the buffer and the amount of data
    available as well. Filters persist across multiple calls to _init()
    but will be emptied by _shutdown().

    \param  hnd             The stream to add the filter to.
    \param  filtfunc        A pointer to the filter function.
    \param  obj             Filter function user data.
*/
void snd_stream_filter_add(snd_stream_hnd_t hnd, snd_stream_filter_t filtfunc,
                           void *obj);

/** \brief  Remove a filter from the specified stream.

    This function removes a filter that was previously added to the specified
    stream.

    \param  hnd             The stream to remove the filter from.
    \param  filtfunc        A pointer to the filter function to remove.
    \param  obj             The filter function's user data. Must be the same as
                            what was passed as obj to snd_stream_filter_add().
*/
void snd_stream_filter_remove(snd_stream_hnd_t hnd,
                              snd_stream_filter_t filtfunc, void *obj);

/** \brief  Prefill the stream buffers.
    \deprecated This function has no effect and should be removed.

    This function has no effect. The stream is prefilled on start.
    This is deprecated and should be removed if used.

*/
static const int __snd_stream_prefill   __depr("snd_stream_prefill has no effect and should be removed") = 0;
#define snd_stream_prefill(x)  ((void)__snd_stream_prefill)

/** \brief  Initialize the stream system.

    This function initializes the sound stream system and allocates memory for
    it as needed. Note, this is not done by the default init, so if you're using
    the streaming support and not using something like the kos-ports Ogg Vorbis
    library, you'll need to call this yourself. This will implicitly call
    snd_init(), so it will potentially overwrite anything going on the AICA.

    \retval -1              On failure.
    \retval 0               On success.
*/
int snd_stream_init(void);

/** \brief  Initialize the stream system with limits.

    The same as \ref snd_stream_init but it can either reduce or not allocate
    the buffer for splitting the stereo stream at all.

    \param  channels        Max channels for any streams.
                            Must be 1 or 2.
    \param  buffer_size     Max channel buffer size for any streams. A nonzero
                            value must be a multiple of 64 bytes. Zero disables
                            the separation buffer, so only direct-transfer
                            callbacks can be started, and leaves per-stream
                            size limited only by sound RAM. The maximum useful
                            value is SND_STREAM_BUFFER_MAX_PCM16.

    \retval -1              On failure.
    \retval 0               On success.
*/
int snd_stream_init_ex(int channels, size_t buffer_size);

/** \brief  Shut down the stream system.

    This function shuts down the stream system and frees the memory associated
    with it. This does not call snd_shutdown().
*/
void snd_stream_shutdown(void);

/** \brief  Allocate a stream.

    This function allocates a stream and sets its parameters.

    \param  cb              The get data callback for the stream.
    \param  bufsize         The size of the buffer for each channel of the
                            stream. Must be positive, a multiple of 32 bytes,
                            no larger than SND_STREAM_BUFFER_MAX_PCM16, and no
                            larger than the initialized limit when nonzero.
    \return                 A handle to the new stream on success,
                            SND_STREAM_INVALID on failure.
*/
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb, int bufsize);

/** \brief  Reinitialize a stream.

    This function reinitializes a stream, resetting its callback function.

    \param  hnd             The stream handle to reinit.
    \param  cb              The new get data callback for the stream.
    \return                 hnd
*/
int snd_stream_reinit(snd_stream_hnd_t hnd, snd_stream_callback_t cb);

/** \brief  Destroy a stream.

    This function destroys a previously created stream, freeing all memory
    associated with it.

    \param  hnd             The stream to clean up.
*/
void snd_stream_destroy(snd_stream_hnd_t hnd);

/** \brief  Enable queueing on a stream.

    This function enables queueing on the specified stream. This will make it so
    that you must call snd_stream_queue_go() to actually start the stream, after
    scheduling the start. This is useful for getting something ready but not
    firing it right away.

    \param  hnd             The stream to enable queueing on.
*/
void snd_stream_queue_enable(snd_stream_hnd_t hnd);

/** \brief  Disable queueing on a stream.

    This function disables queueing on the specified stream. This does not imply
    that a previously queued start on the stream will be fired if queueing was
    enabled before.

    \param  hnd             The stream to disable queueing on.
*/
void snd_stream_queue_disable(snd_stream_hnd_t hnd);

/** \brief  Start a stream after queueing the request.

    This function makes the stream start once a start request has been queued,
    if queueing mode is enabled on the stream.

    \param  hnd             The stream to start the queue on.
*/
void snd_stream_queue_go(snd_stream_hnd_t hnd);

/** \brief Start a queued stream with checked error reporting.

    This submits the synchronized key-on for a stream previously prepared by
    \ref snd_stream_start_ex while queueing was enabled. It does not invoke the
    data callback or wait for a transfer.

    \param hnd             Allocated stream in SND_STREAM_STATE_QUEUED.
    \retval 0              On successful synchronized key-on submission.
    \retval -1             On error with errno set.
*/
int snd_stream_queue_go_ex(snd_stream_hnd_t hnd);

/** \brief  Start a 16-bit PCM stream.

    This function starts processing the given stream, prefilling the buffers as
    necessary. In queueing mode, this will not start playback.

    \param  hnd             The stream to start.
    \param  freq            The frequency of the sound.
    \param  st              1 if the sound is stereo, 0 if mono.
*/
void snd_stream_start(snd_stream_hnd_t hnd, uint32_t freq, int st);

/** \brief  Start a 8-bit PCM stream.

    This function starts processing the given stream, prefilling the buffers as
    necessary. In queueing mode, this will not start playback.

    \param  hnd             The stream to start.
    \param  freq            The frequency of the sound.
    \param  st              1 if the sound is stereo, 0 if mono.
*/
void snd_stream_start_pcm8(snd_stream_hnd_t hnd, uint32_t freq, int st);

/** \brief  Start a 4-bit ADPCM stream.

    This function starts processing the given stream, prefilling the buffers as
    necessary. In queueing mode, this will not start playback.

    \param  hnd             The stream to start.
    \param  freq            The frequency of the sound.
    \param  st              1 if the sound is stereo, 0 if mono.
*/
void snd_stream_start_adpcm(snd_stream_hnd_t hnd, uint32_t freq, int st);

/** \brief Start a stream through the checked channel-control path.

    The stream is fully prefilled before its synchronized key-on is admitted.
    Callback data must end on a complete sample-frame boundary. A short return
    is padded with silence without reading beyond the callback buffer. When
    queueing is enabled, the stream enters SND_STREAM_STATE_QUEUED until
    snd_stream_queue_go_ex() or snd_stream_queue_go() is called.

    This function and other control or poll calls for the same handle must not
    be issued concurrently. Status queries may run independently.

    \param hnd             Allocated stream handle.
    \param config          Complete checked stream configuration.
    \retval 0              On successful prefill and command submission.
    \retval -1             On error with errno set.
*/
int snd_stream_start_ex(snd_stream_hnd_t hnd,
                        const snd_stream_config_t *config);

/** \brief  Stop a stream.

    This function stops a stream, stopping any sound playing from it. This will
    happen immediately, regardless of whether queueing is enabled or not.

    \param  hnd             The stream to stop.
*/
void snd_stream_stop(snd_stream_hnd_t hnd);

/** \brief Stop a stream and bound any pending transfer wait.

    If the transfer deadline expires, the stream remains allocated and must not
    be destroyed until a later stop succeeds. This prevents sound RAM from
    being freed while DMA may still target it.

    \param hnd             Allocated stream handle.
    \param timeout_ms      Nonzero wait bound in milliseconds.
    \retval 0              On a clean stop with no pending transfer.
    \retval -1             On error with errno set.
*/
int snd_stream_stop_ex(snd_stream_hnd_t hnd, uint32_t timeout_ms);

/** \brief  Poll a stream.

    This function polls the specified stream to load more data if necessary. If
    using the streaming support, you must call this function periodically (most
    likely in a thread), or you won't get any sound output.

    \param  hnd             The stream to poll.
    \retval -3              If NULL was returned from the callback.
    \retval -1              If no callback is set, or if the state has been
                            corrupted.
    \retval 0               On success.
*/
int snd_stream_poll(snd_stream_hnd_t hnd);

/** \brief Poll a stream with conventional errno reporting.

    Missing callback data is replaced by silence, increments the underrun
    counter, and returns ENODATA. Transfer and state failures use their native
    errno values instead of the legacy -2/-3 return vocabulary.

    \param hnd             Allocated, started stream handle.
    \retval 0              On success or when no refill is required.
    \retval -1             On error with errno set.
*/
int snd_stream_poll_ex(snd_stream_hnd_t hnd);

/** \brief Update selected live controls on an active stream.

    The field mask uses SND_CHANNEL_UPDATE_* values. Frequency is taken from
    config->sample_rate; other selected fields come from each active channel's
    configuration. Encoding and channel count cannot change until the stream is
    started again.

    \param hnd             Allocated, started stream handle.
    \param config          Source of selected control values.
    \param fields          Nonzero SND_CHANNEL_UPDATE_* mask.
    \retval 0              On successful command submission.
    \retval -1             On error with errno set.
*/
int snd_stream_update(snd_stream_hnd_t hnd,
                      const snd_stream_config_t *config, uint32_t fields);

/** \brief Read one coherent stream lifecycle and progress snapshot.

    source_bytes counts only callback data. buffered_bytes also includes
    silence substituted for short or empty callback results. played_bytes is
    advanced from the firmware channel position whenever the stream is polled
    or queried; as with all ring-buffer streaming, the application must service
    the stream more often than one complete buffer duration to observe wraps.

    \param hnd             Allocated stream handle.
    \param status          Receives the snapshot.
    \retval 0              On success.
    \retval -1             On error with errno set.
*/
int snd_stream_get_status(snd_stream_hnd_t hnd,
                          snd_stream_status_t *status);

/** \brief Destroy a stream after a bounded pending-transfer drain.

    \param hnd             Allocated stream handle.
    \param timeout_ms      Nonzero wait bound in milliseconds.
    \retval 0              On success.
    \retval -1             On error; the stream remains allocated.
*/
int snd_stream_destroy_ex(snd_stream_hnd_t hnd, uint32_t timeout_ms);

/** \brief  Set the volume on the stream.

    This function sets the volume of the specified stream.

    \param  hnd             The stream to set volume on.
    \param  vol             The volume to set. Valid values are 0-255.
*/
void snd_stream_volume(snd_stream_hnd_t hnd, int vol);

/** \brief  Set the panning on the stream.

    This function sets the panning of the specified stream.

    \param  hnd             The stream to set volume on.
    \param  left_pan        The left panning to set. Valid values are 0-255.
    \param  right_pan       The right panning to set. Valid values are 0-255.
*/
void snd_stream_pan(snd_stream_hnd_t hnd, int left_pan, int right_pan);

/** @} */

__END_DECLS

#endif  /* __DC_SOUND_STREAM_H */
