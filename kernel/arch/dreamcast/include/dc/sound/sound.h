/* KallistiOS ##version##

   dc/sound/sound.h
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2023, 2024 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/sound/sound.h
    \brief   Low-level sound support and memory management.
    \ingroup audio_driver

    This file contains declarations for low-level sound operations and for SPU
    RAM pool memory management. Most of the time you'll be better off using the
    higher-level functionality in the sound effect support or streaming support,
    but this stuff can be very useful for some things.

    \author Megan Potter
    \author Ruslan Rostovtsev
*/

#ifndef __DC_SOUND_SOUND_H
#define __DC_SOUND_SOUND_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** \defgroup audio_driver  Driver
    \brief                  Low-level driver for SPU and audio management
    \ingroup                audio

    @{
*/

/** \brief  Allocate memory in the SPU RAM pool

    This function acts as the memory allocator for the SPU RAM pool. It acts
    much like one would expect a malloc() function to act, although it does not
    return a pointer directly, but rather an offset in SPU RAM.

    \param  size            The amount of memory to allocate, in bytes.

    \return                 The location of the start of the block on success,
                            or 0 on failure.
*/
uint32_t snd_mem_malloc(size_t size);

/** \brief  Free a block of allocated memory in the SPU RAM pool.

    This function frees memory previously allocated with snd_mem_malloc().

    \param  addr            The location of the start of the block to free.
*/
void snd_mem_free(uint32_t addr);

/** \brief  Get the size of the largest allocateable block in the SPU RAM pool.

    This function returns the largest size that can be currently passed to
    snd_mem_malloc() and expected to not return failure. There may be more
    memory available in the pool, especially if multiple blocks have been
    allocated and freed, but calls to snd_mem_malloc() for larger blocks will
    return failure, since the memory is not available contiguously.

    \return                 The size of the largest available block of memory in
                            the SPU RAM pool.
*/
uint32_t snd_mem_available(void);

/** \brief  Reinitialize the SPU RAM pool.

    This function reinitializes the SPU RAM pool with the given base offset
    within the memory space. There is generally not a good reason to do this in
    your own code, but the functionality is there if needed.

    \param  reserve         The amount of memory to reserve as a base.
    \retval 0               On success (no failure conditions defined).
*/
int snd_mem_init(uint32_t reserve);

/** \brief  Shutdown the SPU RAM allocator.

    There is generally no reason to be calling this function in your own code,
    as doing so will cause problems if you try to allocate SPU memory without
    calling snd_mem_init() afterwards.
*/
void snd_mem_shutdown(void);

/** \brief Coherent sound-RAM allocator status. */
typedef struct snd_mem_status {
    bool initialized;          /**< Whether the allocator is initialized. */
    uint32_t pool_base;        /**< First allocatable sound-RAM offset. */
    uint32_t pool_size;        /**< Total bytes managed by the allocator. */
    uint32_t allocated_bytes;  /**< Bytes currently allocated. */
    uint32_t free_bytes;       /**< Total bytes in free extents. */
    uint32_t largest_free;     /**< Largest contiguous free extent. */
    uint32_t allocated_blocks; /**< Number of allocated extents. */
    uint32_t free_blocks;      /**< Number of free extents. */
} snd_mem_status_t;

/** \brief Retrieve a coherent sound-RAM allocator snapshot.

    \param status          Receives the allocator snapshot.
    \retval 0              On success.
    \retval -1             On error with errno set.
*/
int snd_mem_get_status(snd_mem_status_t *status);

/** \brief  Initialize the sound system.

    This function reinitializes the whole sound system. It will not do anything
    unless the sound system has been shut down previously or has not been
    initialized yet. This will implicitly replace the program running on the
    AICA's ARM processor when it actually initializes anything. The default
    snd_stream_drv will be loaded if a new program is uploaded to the SPU.
*/
int snd_init(void);

/** \brief  Shut down the sound system.

    This function shuts down the whole sound system, freeing memory and
    disabling the SPU in the process. There's not generally many good reasons
    for doing this in your own code.
*/
void snd_shutdown(void);

/** \defgroup audio_driver_features Driver Feature Flags
    \brief                                Negotiated AICA firmware features
    @{
*/
#define SND_DRIVER_FEATURE_SYNC_CHANNELS 0x00000001u
#define SND_DRIVER_FEATURE_VALIDATION    0x00000002u
#define SND_DRIVER_FEATURE_POSITION      0x00000004u
/** @} */

/** \brief Coherent AICA firmware capability and health snapshot. */
typedef struct snd_driver_status {
    uint32_t protocol_version;       /**< Shared command protocol version. */
    uint32_t firmware_version;       /**< Major.minor.patch as 0x00MMmmpp. */
    uint32_t features;               /**< SND_DRIVER_FEATURE_* bit mask. */
    uint32_t uptime_ms;              /**< Firmware clock in milliseconds. */
    uint32_t commands_processed;     /**< Commands with a valid packet boundary. */
    uint32_t commands_rejected;      /**< Complete but invalid commands. */
    uint32_t malformed_packets;      /**< Queue snapshots dropped for invalid size. */
    uint32_t responses_dropped;      /**< Replies lost to response-queue pressure. */
    uint32_t command_queue_size;     /**< Command queue capacity in bytes. */
    uint32_t command_queue_used;     /**< Pending command bytes at snapshot time. */
    uint32_t response_queue_size;    /**< Response queue capacity in bytes. */
    uint32_t response_queue_used;    /**< Pending response bytes before this reply. */
} snd_driver_status_t;

/** \brief Query the running AICA firmware's capabilities and health.

    This is a bounded command/response operation and must be called from thread
    context. Counters are monotonic modulo 32-bit wrap and describe the current
    firmware instance since snd_init() loaded it. The query preserves pending
    low-level responses and returns EBUSY until their owner consumes them.

    \param status          Receives a coherent firmware snapshot.
    \param timeout_ms      Nonzero overall response deadline in milliseconds.
    \retval 0              On success.
    \retval -1             On error, with errno set to EINVAL, EPERM, ENODEV,
                           EBUSY, EPROTO, EAGAIN, or ETIMEDOUT as appropriate.
*/
int snd_driver_get_status(snd_driver_status_t *status, uint32_t timeout_ms);

/** \brief  Copy a request packet to the AICA queue.

    This function is to put in a low-level request using the built-in streaming
    sound driver.

    \param  packet          The packet of data to copy.
    \param  size            The size of the packet, in 32-bit increments.
    \retval 0               On success.
    \retval -1              On invalid input, unavailable/corrupt shared queue,
                            lock failure, or insufficient queue space. errno is
                            set to EINVAL, ENODEV, EPROTO, or EAGAIN.
*/
int snd_sh4_to_aica(void *packet, uint32_t size);

/** \brief  Begin processing AICA queue requests.

    This function begins processing of any queued requests in the AICA queue.
*/
void snd_sh4_to_aica_start(void);

/** \brief  Stop processing AICA queue requests.

    This function stops the processing of any queued requests in the AICA queue.
*/
void snd_sh4_to_aica_stop(void);

/** \brief Atomically start any selected AICA channels.

    The selected channels must first have been configured with delayed key-on.
    The ARM driver stages the key-on state for the complete 64-channel mask and
    then issues one global key-on execute operation.

    This function only submits the command. A batch bracketed by
    snd_sh4_to_aica_stop() and snd_sh4_to_aica_start() remains stopped until
    the caller explicitly resumes queue processing.

    \param  channels        Bit N selects AICA channel N, from 0 through 63.
    \retval 0               On successful command submission.
    \retval -1              On invalid input or command-queue failure, with
                            errno set appropriately.
*/
int snd_channels_start_sync(uint64_t channels);

/** \brief  Transfer a packet of data from the AICA's SH4 queue.

    This function is used to retrieve a packet of data from the AICA back to the
    SH4. The buffer passed in should at least contain 1024 bytes of space to
    make sure any packet can fit.

    \param  packetout       The buffer to store the retrieved packet in.
    \retval -1              On failure. Failure probably indicates the queue has
                            been corrupted, and thus should be reinitialized.
    \retval 0               If no packets are available.
    \retval 1               On successful copy of one packet.
*/
int snd_aica_to_sh4(void *packetout);

/** \brief  Poll for a response from the AICA.

    This function waits for the AICA to respond to a previously sent request.
    This function is not safe to call in an IRQ, as it does implicitly wait.
*/
void snd_poll_resp(void);

/** \brief  Separates stereo PCM samples into 2 mono channels.

    Splits a buffer containing 2 interleaved channels of 16-bit PCM samples
    into 2 separate buffers of 16-bit PCM samples.

    \warning
    All arguments must be 32-byte aligned.

    \param data   Source buffer of interleaved stereo samples
    \param left   Destination buffer for left mono samples
    \param right  Destination buffer for right mono samples
    \param size   Size of the source buffer in bytes (must be divisible by 32)

    \sa snd_pcm16_split_sq()
*/
void snd_pcm16_split(uint32_t *data, uint32_t *left, uint32_t *right, size_t size);

/** \brief  Separates stereo PCM samples into 2 mono channels with SQ transfer.

    Splits a buffer containing 2 interleaved channels of 16-bit PCM samples
    into 2 separate buffers of 16-bit PCM samples by using the store queues
    for data transfer.

    \warning
    All arguments must be 32-byte aligned.

    \param data   Source buffer of interleaved stereo samples
    \param left   Destination buffer address for left mono samples
    \param right  Destination buffer address for right mono samples
    \param size   Size of the source buffer in bytes (must be divisible by 32)

    \sa snd_pcm16_split()
    Store queues must be prepared before.
*/
void snd_pcm16_split_sq(uint32_t *data, uintptr_t left, uintptr_t right, size_t size);

/** \brief  Separates stereo PCM samples into 2 mono channels.

    Splits a buffer containing 2 interleaved channels of 8-bit PCM samples
    into 2 separate buffers of 8-bit PCM samples.

    \param data   Source buffer of interleaved stereo samples
    \param left   Destination buffer for left mono samples
    \param right  Destination buffer for right mono samples
    \param size   Size of the source buffer in bytes

    \sa snd_adpcm_split()
*/
void snd_pcm8_split(uint32_t *data, uint32_t *left, uint32_t *right, size_t size);

/** \brief  Separates stereo ADPCM samples into 2 mono channels.

    Splits a buffer containing 2 interleaved channels of 4-bit ADPCM samples
    into 2 separate buffers of 4-bit ADPCM samples.

    \param data   Source buffer of interleaved stereo samples
    \param left   Destination buffer for left mono samples
    \param right  Destination buffer for right mono samples
    \param size   Size of the source buffer in bytes

    \sa snd_pcm16_split()
*/
void snd_adpcm_split(uint32_t *data, uint32_t *left, uint32_t *right, size_t size);

/** \brief  Get AICA channel position.

    This function returns actual the channel position
    that stores in SPU memory and updated by the SPU firmware.

    \param  ch              The channel to retrieve position.

    \return                 Last channel position in samples.
*/
uint16_t snd_get_pos(unsigned int ch);

/** \brief  Get AICA channel playback state.

    This function returns actual the channel playback state
    that stores in AICA registers directly.

    \param  ch              The channel to check.

    \return                 True if the channel is playing.
*/
bool snd_is_playing(unsigned int ch);

/** \brief Coherent playback state for one AICA channel. */
typedef struct snd_channel_status {
    uint16_t position;      /**< Last position published by the firmware. */
    bool playing;           /**< Whether the channel key-on bit is active. */
} snd_channel_status_t;

/** \brief Retrieve playback state for one AICA channel.

    Both fields are sampled while holding the G2 bus lock, avoiding the gap
    between separate calls to snd_get_pos() and snd_is_playing().

    \param ch              Channel number in the range 0 through 63.
    \param status          Receives the channel snapshot.
    \retval 0              On success.
    \retval -1             On invalid input with errno set to EINVAL.
*/
int snd_channel_get_status(unsigned int ch, snd_channel_status_t *status);

/** @} */

__END_DECLS

#endif  /* __DC_SOUND_SOUND_H */
