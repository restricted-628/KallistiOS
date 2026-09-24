/* KallistiOS ##version##

   dc/maple/sip.h
   Copyright (C) 2005, 2008, 2010, 2013 Lawrence Sebald
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/maple/sip.h
    \brief   Definitions for using the Sound Input Peripheral.
    \ingroup peripherals_mic

    This file contains the definitions needed to access the Maple microphone
    type device (the Seaman mic). Many thanks go out to ZeZu who pointed me
    toward what some of the commands actually do in the original version of this
    driver.

    As a note, the device itself is actually referred to by the system as the
    Sound Input Peripheral, so hence why this driver is named as it is.

    \author Lawrence Sebald
*/

#ifndef __DC_MAPLE_SIP_H
#define __DC_MAPLE_SIP_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <dc/maple.h>

/** \defgroup peripherals_mic   Microphone
    \brief                      Maple driver for microphone input devices
    \ingroup                    peripherals

    @{
*/

/** \brief  Type for a microphone sample callback.

    This is the signature that is required for a function to accept samples
    from the microphone as it is sampling. This function will be called about
    once per frame, and in an interrupt context (so it should be pretty quick
    to execute). Basically, all you should do in one of these is copy the
    samples out to your own buffer -- do not do any processing on the samples
    in your callback other than to copy them out!

    \param  dev             The device the samples are coming from.
    \param  samples         Pointer to the sample buffer.
    \param  len             The number of bytes in the sample buffer.

    \headerfile dc/maple/sip.h
*/
typedef void (*sip_sample_cb)(maple_device_t *dev, uint8_t *samples, size_t len);

/** \brief  SIP status structure.

    This structure contains information about the status of the microphone
    device and can be fetched with maple_dev_status(). You should not modify
    any of the values in here, it is all "read-only" to your programs. Modifying
    any of this, especially while the microphone is sampling could really screw
    things up.

    \headerfile dc/maple/sip.h
*/
typedef struct  sip_state {
    /** \brief  The gain value for the microphone amp. */
    int             amp_gain;

    /** \brief  The type of samples that are being recorded. */
    int             sample_type;

    /** \brief  What frequency are we sampling at? */
    int             frequency;

    /** \brief  Is the mic currently sampling? */
    bool            is_sampling;

    /** \brief  Sampling callback. */
    sip_sample_cb   callback;
} sip_state_t;

/** \brief State of microphone capture and its most recent control command. */
typedef enum sip_capture_state {
    SIP_CAPTURE_DISCONNECTED = 0, /**< Device was removed. */
    SIP_CAPTURE_STOPPED,          /**< Capture is stopped. */
    SIP_CAPTURE_STARTING,         /**< Start command is pending. */
    SIP_CAPTURE_RECORDING,        /**< Samples are being collected. */
    SIP_CAPTURE_STOPPING,         /**< Stop command is pending. */
    SIP_CAPTURE_ERROR             /**< Most recent start command failed. */
} sip_capture_state_t;

/** \brief Coherent microphone device status snapshot. */
typedef struct sip_device_status {
    int amp_gain;                 /**< Raw amplifier gain, 0 through 31. */
    int sample_type;              /**< Active SIP_SAMPLE_* sample type. */
    int frequency;                /**< Active SIP_SAMPLE_* frequency. */
    sip_capture_state_t state;    /**< Current capture state. */
    int last_result;              /**< Result of the last control command. */
    int last_response;            /**< Raw Maple response to the last command. */
    uint32_t sequence;            /**< Status-change sequence number. */
    uint64_t packets_received;    /**< Valid sample packets received. */
    uint64_t samples_received;    /**< Complete samples received. */
    uint64_t malformed_responses; /**< Rejected or truncated responses. */
    uint64_t command_failures;    /**< Rejected start or stop commands. */
    uint64_t command_timeouts;    /**< Blocking command waits that timed out. */
    uint32_t sample_status;       /**< Raw status word from the last packet. */
} sip_device_status_t;

/** \brief Opaque caller-buffered microphone capture object. */
typedef struct sip_capture sip_capture_t;

/** \brief Opaque independent reader of a microphone capture ring. */
typedef struct sip_stream sip_stream_t;

/** \brief Snapshot of a caller-buffered microphone capture. */
typedef struct sip_capture_status {
    sip_capture_state_t state;    /**< Current capture state. */
    size_t buffer_size;           /**< Ring capacity in bytes. */
    size_t sample_size;           /**< Bytes per recorded sample. */
    size_t buffered_samples;      /**< Samples currently retained. */
    uint64_t write_position;      /**< Samples received since start. */
    uint64_t packets_received;    /**< Packets written since start. */
    uint64_t samples_received;    /**< Samples written since start. */
    uint32_t sample_status;       /**< Raw status word from the last packet. */
} sip_capture_status_t;

/** \brief Snapshot of one independent capture-ring reader. */
typedef struct sip_stream_status {
    uint64_t read_position;       /**< Reader position in samples. */
    size_t available_samples;     /**< Samples readable now. */
    uint64_t lost_samples;        /**< Samples overwritten before reading. */
    bool overrun;                 /**< Sticky reader-overrun indication. */
    bool disconnected;            /**< Capture device was removed. */
} sip_stream_status_t;

/** \defgroup sip_stream_seek Microphone stream seek origins
    \ingroup peripherals_mic
    @{
*/
#define SIP_STREAM_SEEK_OLDEST 0 /**< Offset from oldest retained sample. */
#define SIP_STREAM_SEEK_CURRENT 1 /**< Offset from current reader position. */
#define SIP_STREAM_SEEK_LATEST 2 /**< Offset from current write position. */
/** @} */

/** \brief  Get recorded samples from the microphone device.

    This subcommand is used with the MAPLE_COMMAND_MICCONTROL command to fetch
    samples from the microphone.
*/
#define SIP_SUBCOMMAND_GET_SAMPLES 0x01

/** \brief  Start and stop sampling.

    This subcommand is used with the MAPLE_COMMAND_MICCONTROL command to start
    and stop sampling on the microphone.
*/
#define SIP_SUBCOMMAND_BASIC_CTRL  0x02

/** \brief  Minimum microphone gain. */
#define SIP_MIN_GAIN     0x00

/** \brief  Default microphone gain. */
#define SIP_DEFAULT_GAIN 0x0F

/** \brief  Maximum microphone gain. */
#define SIP_MAX_GAIN     0x1F

/** \brief  Set the microphone's gain value.

    This function sets the gain value of the specified microphone device to
    the value given. This should only be called prior to sampling so as to keep
    the amplification constant throughout the sampling process, but can be
    changed on the fly if you really want to.

    \param  dev             The microphone device to set gain on.
    \param  g               The value to set as the gain.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EINVALID  If g is out of range.
    \see    SIP_MIN_GAIN
    \see    SIP_DEFAULT_GAIN
    \see    SIP_MAX_GAIN
*/
int sip_set_gain(maple_device_t *dev, unsigned int g);

/* Sample types. These two values are the only defined types of samples that
   the SIP can output. 16-bit signed is your standard 16-bit signed samples,
   where 8-bit ulaw is obvously encoded as ulaw. */

/** \brief  Record 16-bit signed integer samples. */
#define SIP_SAMPLE_16BIT_SIGNED 0x00

/** \brief  Record 8-bit ulaw samples. */
#define SIP_SAMPLE_8BIT_ULAW    0x01

/** \brief  Set the sample type to be recorded by the microphone.

    This function sets the sample type that the microphone will return. The
    default value for this is 16-bit signed integer samples. You must call this
    prior to sip_start_sampling() if you wish to change it from the default.

    \param  dev             The microphone device to set sample type on.
    \param  type            The type of samples requested.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EINVALID  If type is invalid.
    \retval MAPLE_EFAIL     If the microphone is sampling.
    \see    SIP_SAMPLE_16BIT_SIGNED
    \see    SIP_SAMPLE_8BIT_ULAW
*/
int sip_set_sample_type(maple_device_t *dev, unsigned int type);

/* Sampling frequencies. The SIP supports sampling at either 8kHz or 11.025 kHz.
   One of these values should be passed to the sip_set_frequency function. */
/** \brief  Record samples at 11.025kHz. */
#define SIP_SAMPLE_11KHZ 0x00

/** \brief  Record samples at 8kHz. */
#define SIP_SAMPLE_8KHZ  0x01

/** \brief  Set the sample frequency to be recorded by the microphone.

    This function sets the sample frequency that the microphone will record. The
    default value for this is about 11.025kHz samples. You must call this prior
    to sip_start_sampling() if you wish to change it from the default.

    \param  dev             The microphone device to set sample type on.
    \param  freq            The type of samples requested.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EINVALID  If freq is invalid.
    \retval MAPLE_EFAIL     If the microphone is sampling.
    \see    SIP_SAMPLE_11KHZ
    \see    SIP_SAMPLE_8KHZ
*/
int sip_set_frequency(maple_device_t *dev, unsigned int freq);

/** \brief Copy a coherent status snapshot for a microphone device.

    Unlike direct reads of \ref sip_state_t, this function excludes the Maple
    interrupt while copying fields which are updated by response callbacks.

    \param dev              Microphone device.
    \param status           Destination status object.
    \retval 0               Snapshot copied.
    \retval -1              Invalid or detached device; errno is set.
*/
int sip_get_status(maple_device_t *dev, sip_device_status_t *status);

/** \brief  Start sampling on a microphone.

    This function informs a microphone it should start recording samples.

    \param  dev             The device to start sampling on.
    \param  cb              A callback to call when samples are ready.
    \param  block           Set to true to wait for the SIP to start sampling.
                            Otherwise check the is_sampling member of the status
                            for dev to know when it has started.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EAGAIN    If the command couldn't be sent, try again later.
    \retval MAPLE_EFAIL     If the microphone is already sampling or the
                            callback function is NULL.
    \retval MAPLE_ETIMEOUT  If the command timed out while blocking.
*/
int sip_start_sampling(maple_device_t *dev, sip_sample_cb cb, bool block);

/** \brief  Stop sampling on a microphone.

    This function informs a microphone it should stop recording samples.

    \param  dev             The device to stop sampling on.
    \param  block           Set to true to wait for the SIP to stop sampling.
                            Otherwise check the is_sampling member of the status
                            for dev to know when it has finished.
    \retval MAPLE_EOK       On success.
    \retval MAPLE_EAGAIN    If the command couldn't be sent, try again later.
    \retval MAPLE_EFAIL     If the microphone is not sampling.
    \retval MAPLE_ETIMEOUT  If the command timed out while blocking.
*/
int sip_stop_sampling(maple_device_t *dev, bool block);

/** \brief Attach a caller-owned ring buffer to a microphone.

    No worker thread or internal sample buffer is allocated. The returned
    control object is small and exists only while the caller uses buffered
    capture. The buffer must remain valid until \ref sip_capture_destroy.

    Buffered-capture lifecycle, status, seek, and read functions are ordinary
    thread-context APIs. Only the legacy sample callback runs in interrupt
    context.

    Configure sample type and frequency before creating the capture. The
    buffer size must be nonzero, even, and a multiple of the active sample
    size.

    \param dev              Stopped microphone device.
    \param buffer           Caller-owned ring storage.
    \param buffer_size      Ring capacity in bytes.
    \return                 New capture object, or NULL with errno set.
*/
sip_capture_t *sip_capture_create(maple_device_t *dev, void *buffer,
                                  size_t buffer_size);

/** \brief Destroy a stopped microphone capture object.

    All streams must be closed first. Destruction must not race another
    operation using the same capture object.

    \retval 0               Capture destroyed.
    \retval -1              Capture is active, has readers, or is invalid.
*/
int sip_capture_destroy(sip_capture_t *capture);

/** \brief Start caller-buffered microphone capture.

    Existing stream positions and overrun counters are reset when a new
    recording begins. The legacy sample callback is not required.

    \param capture          Capture object.
    \param block            Wait up to 500 ms for the device response.
    \return                 A MAPLE_E* result.
*/
int sip_capture_start(sip_capture_t *capture, bool block);

/** \brief Stop caller-buffered microphone capture.

    Retained samples remain readable until the next start or destruction.

    \param capture          Capture object.
    \param block            Wait up to 500 ms for the device response.
    \return                 A MAPLE_E* result.
*/
int sip_capture_stop(sip_capture_t *capture, bool block);

/** \brief Copy a coherent caller-buffered capture snapshot. */
int sip_capture_get_status(sip_capture_t *capture,
                           sip_capture_status_t *status);

/** \brief Open an independent reader on a capture ring.

    \param capture          Capture object.
    \param from_latest      Start at the current write position when true, or
                            the oldest retained sample when false.
    \return                 New reader, or NULL with errno set.
*/
sip_stream_t *sip_stream_open(sip_capture_t *capture, bool from_latest);

/** \brief Close and destroy an independent capture reader. */
int sip_stream_close(sip_stream_t *stream);

/** \brief Copy a coherent independent-reader snapshot. */
int sip_stream_get_status(sip_stream_t *stream,
                          sip_stream_status_t *status);

/** \brief Read and consume complete samples without blocking.

    The return value is measured in samples, not bytes. Sixteen-bit samples
    are copied in the device's byte order. If the writer overtakes this reader,
    reading resumes at the oldest retained complete sample and the lost count
    is updated.

    \return                 Samples copied, zero when empty, or -1 on error.
*/
ssize_t sip_stream_read(sip_stream_t *stream, void *buffer, size_t samples);

/** \brief Seek within the samples currently retained by the ring.

    \param stream           Reader object.
    \param offset           Signed sample offset from \p whence.
    \param whence           One of the SIP_STREAM_SEEK_* constants.
    \retval 0               Reader repositioned.
    \retval -1              Target is outside retained data; errno is set.
*/
int sip_stream_seek(sip_stream_t *stream, int64_t offset, int whence);

/** \brief Clear a reader's sticky overrun flag and lost-sample counter. */
int sip_stream_clear_overrun(sip_stream_t *stream);

/* \cond */
/* Init / Shutdown */
void sip_init(void);
void sip_shutdown(void);
/* \endcond */

/** @} */

__END_DECLS

#endif  /* __DC_MAPLE_SIP_H */
