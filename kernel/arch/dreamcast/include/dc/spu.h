/* KallistiOS ##version##

   dc/spu.h
   Copyright (C) 2000, 2001 Megan Potter
   Copyright (C) 2023, 2024 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/spu.h
    \brief   Functions related to sound.
    \ingroup audio_driver

    This file deals with memory transfers and the like for the sound hardware.

    \author Megan Potter
    \author Ruslan Rostovtsev
*/

#ifndef __DC_SPU_H
#define __DC_SPU_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <dc/memory.h>
#include <dc/g2bus.h>

/** \addtogroup audio_driver
    @{
*/

/** \brief  Sound ram address from the SH4 side */
#define SPU_RAM_BASE 0x00800000
#define SPU_RAM_UNCACHED_BASE (MEM_AREA_P2_BASE | SPU_RAM_BASE)

/** \brief  Copy a block of data to sound RAM.

    This function acts much like memcpy() but copies to the sound RAM area.

    \param  to              The offset in sound RAM to copy to. Do not include
                            the 0xA0800000 part, it is implied.
    \param  from            A pointer to copy from.
    \param  length          The exact number of bytes to copy.
*/
void spu_memload(uintptr_t to, const void *from, size_t length);


/** \brief  Copy a block of data to sound RAM by using the Store Queues.

    This function acts much like memcpy() but copies to the sound RAM area
    by using the Store Queues.

    \param  to              The offset in sound RAM to copy to. Do not include
                            the 0xA0800000 part, it is implied.
    \param  from            A pointer to copy from.
    \param  length          The exact number of bytes to copy.
*/
void spu_memload_sq(uintptr_t to, const void *from, size_t length);

/** \brief  Copy a block of data to sound RAM by using DMA (or SQ on fails).

    This function acts much like memcpy() but copies to the sound RAM area
    by using the DMA. If DMA fails, then will be used the Store Queues.

    \param  to              The offset in sound RAM to copy to. Do not include
                            the 0xA0800000 part, it is implied.
    \param  from            A pointer to copy from.
    \param  length          The number of bytes to copy. Must be a multiple of 32.
*/
void spu_memload_dma(uintptr_t to, const void *from, size_t length);

/** \brief  Copy a block of data from sound RAM.

    This function acts much like memcpy() but copies from the sound RAM area.

    \param  to              A pointer to copy to.
    \param  from            The offset in sound RAM to copy from. Do not include
                            the 0xA0800000 part, it is implied.
    \param  length          The exact number of bytes to copy.
*/
void spu_memread(void *to, uintptr_t from, size_t length);

/** \brief  Set a block of sound RAM to the specified value.

    This function acts like memset4(), setting the specified block of sound RAM
    to the given 32-bit value.

    \param  to              The offset in sound RAM to set at. Do not include
                            the 0xA0800000 part, it is implied.
    \param  what            The value to set.
    \param  length          The exact number of bytes to set. Partial final
                            words preserve bytes beyond this range.
*/
void spu_memset(uintptr_t to, uint32_t what, size_t length);


/** \brief  Set a block of sound RAM to the specified value.

    This function acts like memset4(), setting the specified block of sound RAM
    to the given 32-bit value by using the Store Queues.

    \param  to              The offset in sound RAM to set at. Do not include
                            the 0xA0800000 part, it is implied.
    \param  what            The value to set.
    \param  length          The exact number of bytes to set. Partial final
                            words preserve bytes beyond this range.
*/
void spu_memset_sq(uintptr_t to, uint32_t what, size_t length);

/* DMA copy from SH-4 RAM to SPU RAM; length must be a multiple of 32,
   and the source and destination addresses must be aligned on 32-byte
   boundaries. If block is non-zero, this function won't return until
   the transfer is complete. If callback is non-NULL, it will be called
   upon completion (in an interrupt context!). Returns <0 on error. */

/** \brief  SPU DMA callback type. */
typedef g2_dma_callback_t spu_dma_callback_t;

/** \brief  Copy a block of data from SH4 RAM to sound RAM via DMA.

    This function sets up a DMA transfer from main RAM to the sound RAM with G2
    DMA.

    \param  from            A pointer in main RAM to transfer from. Must be
                            32-byte aligned.
    \param  dest            Offset in sound RAM to transfer to. Do not include
                            the 0xA0800000 part, its implied. Must be 32-byte
                            aligned.
    \param  length          Number of bytes to copy. Must be a multiple of 32.
    \param  block           1 if you want to wait for the transfer to complete,
                            0 otherwise (use the callback for this case).
    \param  callback        Function to call when the DMA completes. Can be NULL
                            if you don't want to have a callback. This will be
                            called in an interrupt context, so keep that in mind
                            when writing the function.
    \param  cbdata          Data to pass to the callback function.
    \retval -1              On failure. Sets errno as appropriate.
    \retval 0               On success.

    \par    Error Conditions:
    \em     EINVAL - Invalid channel \n
    \em     EFAULT - from or dest is not aligned \n
    \em     EIO - I/O error
*/
int spu_dma_transfer(void *from, uintptr_t dest, size_t length, int block,
                     spu_dma_callback_t callback, void *cbdata);

/** \brief Direction of an asynchronous sound-RAM transfer. */
typedef enum spu_transfer_direction {
    /** Copy from main RAM to sound RAM. */
    SPU_TRANSFER_TO_SOUND_RAM = 0,
    /** Copy from sound RAM to main RAM. */
    SPU_TRANSFER_FROM_SOUND_RAM
} spu_transfer_direction_t;

/** \brief Requested transport for an asynchronous sound-RAM transfer. */
typedef enum spu_transfer_method {
    /** Select DMA when the complete transfer is compatible, otherwise PIO. */
    SPU_TRANSFER_AUTO = 0,
    /** Use exact-byte programmed I/O in bounded chunks. */
    SPU_TRANSFER_PIO,
    /** Require a 32-byte-aligned G2 DMA transfer. */
    SPU_TRANSFER_DMA
} spu_transfer_method_t;

/** \brief State of an asynchronous sound-RAM transfer request. */
typedef enum spu_transfer_state {
    SPU_TRANSFER_QUEUED = 0,  /**< Waiting for the sound transfer worker. */
    SPU_TRANSFER_RUNNING,     /**< PIO or DMA is active. */
    SPU_TRANSFER_COMPLETE,    /**< All requested bytes are visible. */
    SPU_TRANSFER_CANCELLED,   /**< Cancelled by the caller or shutdown. */
    SPU_TRANSFER_TIMED_OUT,   /**< Execution deadline expired. */
    SPU_TRANSFER_ERROR        /**< Transfer failed; inspect result. */
} spu_transfer_state_t;

/** \brief Coherent status snapshot for a sound-RAM transfer request. */
typedef struct spu_transfer_status {
    spu_transfer_state_t state;       /**< Current request state. */
    spu_transfer_direction_t direction; /**< Transfer direction. */
    spu_transfer_method_t requested_method; /**< Caller-selected policy. */
    spu_transfer_method_t active_method; /**< Transport actually selected. */
    size_t requested_bytes;           /**< Total exact byte count. */
    size_t completed_bytes;           /**< Valid prefix copied so far. */
    int result;                       /**< Zero or a positive errno value. */
    bool callback_pending;            /**< Callback queued or executing. */
} spu_transfer_status_t;

/** \brief Opaque asynchronous sound-RAM transfer request. */
typedef struct spu_transfer_request spu_transfer_request_t;

/** \brief Thread-context completion callback for sound-RAM transfers.

    The callback runs on a lazily created dispatcher thread, never in interrupt
    context and never on the transfer worker. The request remains owned by the
    caller. It cannot be destroyed until the callback returns. Callbacks must
    remain bounded and must not invoke `spu_shutdown()`; that call fails with
    `EDEADLK` from the dispatcher context.
*/
typedef void (*spu_transfer_callback_t)(spu_transfer_request_t *request,
                                        const spu_transfer_status_t *status,
                                        void *data);

/** \brief Submit an asynchronous transfer between main RAM and sound RAM.

    The transfer system and its two worker stacks are created on first use.
    Ordinary synchronous SPU users therefore pay no worker or queue cost. The
    caller owns both the returned request and the main-RAM range, which must
    remain valid until the request reaches a terminal state. For uploads, the
    source must also remain unmodified until then.

    This is a transport API, not a sound-RAM allocator. The caller must own the
    complete sound-RAM range and must not overlap the running ARM program,
    command queues, allocated samples, or active stream buffers. Initialization
    and shutdown of a sound runtime must not race outstanding raw transfers.

    `SPU_TRANSFER_AUTO` selects DMA only when the main-RAM pointer, sound-RAM
    offset, and length are all 32-byte aligned; all other exact-byte transfers
    use PIO. `SPU_TRANSFER_DMA` rejects incompatible alignment rather than
    rounding or silently changing transport. PIO is split into bounded chunks,
    so progress and cancellation remain observable between chunks.

    Sound-RAM offsets are validated against the physical RAM capacity reported
    by the console mode. A timeout of zero disables the execution deadline;
    nonzero values start when the request is admitted and cover both queue time
    and transfer time. Waiting has its own independent timeout and never
    cancels a request automatically.

    \param direction         Copy direction.
    \param sound_address     Byte offset in sound RAM; no mapped base included.
    \param memory            Main-RAM source or destination.
    \param length            Nonzero exact byte count.
    \param method            Requested transfer policy.
    \param execution_timeout Maximum total lifetime in milliseconds, or zero.
    \param callback          Optional terminal callback.
    \param callback_data     Opaque callback argument.
    \param request           Receives the caller-owned request.
    \retval 0                Request admitted.
    \retval -1               Validation, allocation, or startup failed.
*/
int spu_transfer_submit(spu_transfer_direction_t direction,
                        uintptr_t sound_address, void *memory, size_t length,
                        spu_transfer_method_t method,
                        uint32_t execution_timeout,
                        spu_transfer_callback_t callback,
                        void *callback_data,
                        spu_transfer_request_t **request);

/** \brief Submit an asynchronous main-RAM to sound-RAM transfer. */
int spu_memload_async(uintptr_t to, const void *from, size_t length,
                      spu_transfer_method_t method,
                      uint32_t execution_timeout,
                      spu_transfer_callback_t callback, void *callback_data,
                      spu_transfer_request_t **request);

/** \brief Submit an asynchronous sound-RAM to main-RAM transfer. */
int spu_memread_async(void *to, uintptr_t from, size_t length,
                      spu_transfer_method_t method,
                      uint32_t execution_timeout,
                      spu_transfer_callback_t callback, void *callback_data,
                      spu_transfer_request_t **request);

/** \brief Copy the current status of a sound-RAM transfer request.

    DMA progress is sampled directly from the active G2 channel. PIO progress
    advances after each bounded chunk. On every terminal state, the range
    `[0, completed_bytes)` in the main-RAM endpoint reflects bytes actually
    consumed by an upload or made visible by a readback.
*/
int spu_transfer_get_status(const spu_transfer_request_t *request,
                            spu_transfer_status_t *status);

/** \brief Wait for a sound-RAM transfer request to become terminal.

    A timeout of zero waits indefinitely. A wait timeout returns `ETIMEDOUT`
    without changing the request. A terminal cancellation, execution timeout,
    or error returns -1 with errno set from the request result.
*/
int spu_transfer_wait(spu_transfer_request_t *request, uint32_t timeout,
                      spu_transfer_status_t *status);

/** \brief Wait until the optional completion callback has returned.

    A timeout of zero waits indefinitely. Calling this from the sound callback
    dispatcher returns `EDEADLK`.
*/
int spu_transfer_wait_callback(spu_transfer_request_t *request,
                               uint32_t timeout);

/** \brief Request cancellation of a queued or active transfer.

    Active DMA is stopped immediately. Active PIO observes cancellation after
    its current bounded chunk. Cancellation is best-effort: a transfer which
    wins the terminal race can still complete. A completed request returns
    `EALREADY`.
*/
int spu_transfer_cancel(spu_transfer_request_t *request);

/** \brief Destroy a caller-owned terminal transfer request.

    Destruction fails with `EBUSY` while the request, a waiter, or its callback
    is active. In particular, a request cannot destroy itself from its callback.
*/
int spu_transfer_destroy(spu_transfer_request_t *request);

/** \brief  Enable the SPU.

    This function resets all sound channels and lets the ARM out of reset.
*/
void spu_enable(void);

/** \brief  Disable the SPU.

    This function resets all sound channels and puts the ARM in a reset state.
*/
void spu_disable(void);

/** \brief  Set CDDA volume.

    Valid volume values are 0-15.

    \param  left_volume     Volume of the left channel.
    \param  right_volume    Volume of the right channel.
*/
void spu_cdda_volume(int left_volume, int right_volume);

/** \brief  Set CDDA panning.

    Valid values are from 0-31. 16 is centered.

    \param  left_pan        Pan of the left channel.
    \param  right_pan       Pan of the right channel.
*/
void spu_cdda_pan(int left_pan, int right_pan);

/** \brief  Set master mixer settings.

    This function sets the master mixer volume and mono/stereo setting.

    \param  volume          The volume to set (0-15).
    \param  stereo          1 for stereo output, 0 for mono.
*/
void spu_master_mixer(int volume, int stereo);

/** \brief  Initialize the SPU.

    This function will reset the SPU, clear the sound RAM, reinit the CDDA
    support and run an infinite loop on the ARM.

    \retval 0               On success (no error conditions defined).
*/
int spu_init(void);

/** \brief  Shutdown the SPU.

    This function disables the SPU and clears sound RAM.

    \retval 0               On success (no error conditions defined).
*/
int spu_shutdown(void);

/** \brief  Reset SPU channels. */
void spu_reset_chans(void);

/** @} */

__END_DECLS

#endif  /* __DC_SPU_H */
