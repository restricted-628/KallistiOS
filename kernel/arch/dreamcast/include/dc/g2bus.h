/* KallistiOS ##version##

   g2bus.h
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2023 Andy Barajas
   Copyright (C) 2024 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/g2bus.h
    \brief   G2 bus memory interface.
    \ingroup system_g2bus

    This file provides low-level support for accessing devices on the G2 bus in
    the Dreamcast. The G2 bus contains the AICA, as well as the expansion port.
    Generally, you won't be dealing with things at this level, but rather on the
    level of the device you're actually interested in working with. Most of the
    expansion port devices (the modem, bba, and lan adapter) all have their own
    drivers that work off of this functionality.

    The G2 bus is notoroiously picky about a lot of things. You have to be
    careful to use the right access size for whatever you're working with. Also
    you can't be doing PIO and DMA at the same time. Finally, there's a FIFO to
    contend with when you're doing PIO stuff as well. Generally, G2 is a pain in
    the rear, so you really do want to be using the higher-level stuff related
    to each device if at all possible!

    \author Megan Potter
    \author Andy Barajas
    \author Joseph Black
*/

#ifndef __DC_G2BUS_H
#define __DC_G2BUS_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <stdbool.h>
#include <kos/irq.h>

#include <dc/fifo.h>

/** \defgroup system_g2bus  G2 Bus
    \brief                  Driver for accessing the devices on the G2 Bus
    \ingroup                system

    @{
*/

/** \name       List of G2 Bus channels

    AICA (SPU) is channel 0, BBA uses channel 1. CH2_DMA_G2CHN and
    CH3_DMA_G2CHN are not currently tied to any specific device.

    \note
      A change in the implementation has rendered *_DMA_MODE and *_DMA_SHCHN
      obsolete.

      In the current implementation, *_DMA_MODE should always be set to zero
      (representing CPU_TRIGGER). There is also no involvement of SH4-DMA with
      G2-DMA; therefore, the *_DMA_SHCHN values have been deprecated.

    @{
*/
#define G2_DMA_CHAN_SPU  0 /**< \brief AICA: G2 channel 0 */
#define G2_DMA_CHAN_BBA  1 /**< \brief BBA:  G2 channel 1 */
#define G2_DMA_CHAN_CH2  2 /**< \brief CH2: G2 channel 2 */
#define G2_DMA_CHAN_CH3  3 /**< \brief CH3: G2 channel 3 */
/** @} */

/** \brief  G2Bus DMA direction

    The direction you want the data to go. SH4 to AICA, BBA, etc use
    SH4TOG2BUS, otherwise G2BUSTOSH4.
*/
#define G2_DMA_TO_G2   0
#define G2_DMA_TO_SH4  1

/** \brief  G2Bus DMA interrupt callback type.

    Functions that act as callbacks when G2-DMA completes should be of this type.
    These functions will be called inside an interrupt context, so don't try to
    use anything that might stall.

    \param  data            User data passed in to the g2_dma_transfer()
                            function.
*/
typedef void (*g2_dma_callback_t)(void *data);

/** \brief State of a G2 DMA channel. */
typedef enum g2_dma_state {
    G2_DMA_STATE_IDLE = 0,   /**< No transfer has been submitted. */
    G2_DMA_STATE_RUNNING,    /**< Transfer is active. */
    G2_DMA_STATE_SUSPENDED,  /**< Transfer has been explicitly suspended. */
    G2_DMA_STATE_COMPLETE,   /**< Most recent transfer completed. */
    G2_DMA_STATE_CANCELLED   /**< Most recent transfer was cancelled. */
} g2_dma_state_t;

/** \brief Coherent status snapshot for one G2 DMA channel. */
typedef struct g2_dma_status {
    uint32_t channel;          /**< G2 DMA channel number. */
    g2_dma_state_t state;      /**< Current software-visible state. */
    size_t requested_bytes;    /**< Size submitted for the current operation. */
    size_t remaining_bytes;    /**< Hardware-reported bytes remaining. */
    uint64_t sequence;         /**< Sequence number of the current operation. */
    uint64_t completions;      /**< Successfully completed operations. */
    uint64_t cancellations;    /**< Cancelled operations. */
    int result;                /**< Zero or the errno value for the result. */
    bool callback_pending;     /**< Completion callback has not run yet. */
} g2_dma_status_t;

/** \brief  Perform a DMA transfer between SH-4 RAM and G2 Bus

    This function copies a block of data between SH-4 RAM and G2 Bus via DMA.
    You specify the direction of the copy (SH4TOG2BUS/G2BUSTOSH4). There are all
    kinds of constraints that must be fulfilled to actually do this, so
    make sure to read all the fine print with the parameter list.

    If a callback is specified, it will be called in an interrupt context, so
    keep that in mind in writing the callback.

    Cacheable SH-4 source ranges are written back before DMA. Cacheable
    destination ranges are invalidated before submission and again after the
    engine reaches a terminal state. When the MMU is enabled, the SH-4 range
    must use a direct P1 or P2 alias; translated P0/P3 mappings are rejected
    because a single G2 DMA operation cannot represent a non-contiguous span.
    The G2 endpoint is a bus address rather than an SH-4 virtual mapping, so a
    physical G2 address or its ordinary P1/P2/P3 alias remains valid with the
    MMU enabled.

    \param  sh4             Main-RAM source/destination. Must be 32-byte
                            aligned and large enough for \p length.
    \param  g2bus           Where to copy from/to. Must be 32-byte aligned.
    \param  length          Nonzero number of bytes to copy. Must be a multiple
                            of 32; invalid sizes are rejected, not rounded.
    \param  block           Non-zero if you want the function to block until the
                            DMA completes.
    \param  callback        A function to call upon completion of the DMA.
    \param  cbdata          Data to pass to the callback function.
    \param  dir             SH4TOG2BUS or G2BUSTOSH4.
    \param  mode            Ignored; for compatibility only.
    \param  g2chn           See g2b_channels.
    \param  sh4chn          Ignored; for compatibility only.
    \retval 0               On success.
    \retval -1              On failure. Sets errno as appropriate.

    \par    Error Conditions:
    \em     ENODEV - G2 DMA support is not initialized \n
    \em     EINPROGRESS - DMA already in progress \n
    \em     EFAULT - address alignment, range, or alias is invalid \n
    \em     EINVAL - invalid channel, direction, or transfer length \n
    \em     EPERM - blocking submission from interrupt context \n
    \em     EIO - terminal state could not be reconciled

*/
int g2_dma_transfer(void *sh4, void *g2bus, size_t length, uint32_t block,
                    g2_dma_callback_t callback, void *cbdata,
                    uint32_t dir, uint32_t mode, uint32_t g2chn, uint32_t sh4chn);

/** \brief Copy a coherent G2 DMA channel status snapshot.

    This function does not service or advance the transfer. Progress is driven
    by the DMA engine and its completion interrupt. The output is zeroed before
    validation when it is non-NULL. `callback_pending` can remain true after
    the transfer reaches `G2_DMA_STATE_COMPLETE`, because the terminal state is
    published before the interrupt-context callback runs.

    \retval 0               Status copied.
    \retval -1              Invalid channel or output pointer; errno is set.
*/
int g2_dma_get_status(uint32_t channel, g2_dma_status_t *status);

/** \brief Wait for the active G2 DMA operation to reach a terminal state.

    A timeout of zero waits indefinitely. This function is only valid in
    ordinary thread context. One explicit waiter is allowed per active channel;
    a transfer submitted with `block` already owns that wait role. Once a
    channel is terminal, any caller may inspect or wait on the result without
    consuming it.

    \retval 0               Operation completed successfully.
    \retval -1              No operation, timeout, cancellation, or error.
*/
int g2_dma_wait(uint32_t channel, uint32_t timeout);

/** \brief Suspend an active G2 DMA channel.

    \retval 0               Channel entered the suspended state.
    \retval -1              Invalid/uninitialized channel, or no running
                            operation; `errno` is set appropriately.
*/
int g2_dma_suspend(uint32_t channel);

/** \brief Resume a G2 DMA channel suspended by \ref g2_dma_suspend.

    \retval 0               Channel resumed.
    \retval -1              Invalid/uninitialized channel, or no suspended
                            operation; `errno` is set appropriately.
*/
int g2_dma_resume(uint32_t channel);

/** \brief Cancel the active G2 DMA operation.

    Cancellation disables the channel and wakes a blocking or timed waiter.
    A completion callback is not invoked for a cancelled transfer.

    \retval 0               Active transfer cancelled.
    \retval -1              Invalid/uninitialized channel, or no active
                            transfer; `errno` is set appropriately.
*/
int g2_dma_cancel(uint32_t channel);

/** \brief  Initialize DMA support.

    This function sets up the DMA support for transfers to/from the G2 Bus.
    It must be called from ordinary thread context. Concurrent lifecycle
    changes are rejected rather than exposing partially initialized channels.

    \retval 0               On success.
    \retval -1              Initialization failed; `errno` is set.

    \par    Error Conditions:
    \em     EBUSY - initialization or shutdown is already in progress \n
    \em     EPERM - called from interrupt context
*/
int g2_dma_init(void);

/** \brief  Shutdown DMA support.

    This function must be called from ordinary thread context. A call made
    from interrupt context is ignored and sets `errno` to `EPERM`. A call that
    races another lifecycle operation is ignored and sets `errno` to `EBUSY`.
*/
void g2_dma_shutdown(void);

/** \brief  G2 context

    A G2 context containing the states of IRQs and G2 DMA. This struct
    is used in with g2_lock() and g2_unlock().
*/
typedef struct {
    irq_mask_t irq_state;    /**< IRQ state on entry. */
    uint32_t dma_suspend[4];  /**< Exact per-channel suspend-register state. */
} g2_ctx_t;

/* Internal register access boundary. The indirection keeps the inline lock
   semantics testable without changing production MMIO behavior. */
/** \cond */
#define G2_DMA_SUSPEND_ADDR(channel) \
    (UINT32_C(0xa05f781c) + (uint32_t)(channel) * UINT32_C(0x20))
#ifndef G2_DMA_SUSPEND_READ
#define G2_DMA_SUSPEND_READ(channel) \
    (*((volatile uint32_t *)G2_DMA_SUSPEND_ADDR(channel)))
#define __G2_DMA_SUSPEND_READ_LOCAL
#endif
#ifndef G2_DMA_SUSPEND_WRITE
#define G2_DMA_SUSPEND_WRITE(channel, value) \
    (*((volatile uint32_t *)G2_DMA_SUSPEND_ADDR(channel)) = (value))
#define __G2_DMA_SUSPEND_WRITE_LOCAL
#endif
/** \endcond */

/** \brief  Disable IRQs and G2 DMA

    This function makes the following g2_read_*()/g2_write_*() functions atomic
    by disabling IRQs, preserving all four DMA suspend registers, suspending
    every channel, and draining the FIFO. Pass the returned context to
    g2_unlock() to restore the exact prior state.

    \return                 The context containing the IRQ and G2 DMA states.
*/
static inline g2_ctx_t g2_lock(void) {
    g2_ctx_t ctx;
    uint32_t channel;

    ctx.irq_state = irq_disable();

    for(channel = 0; channel < 4; ++channel) {
        ctx.dma_suspend[channel] = G2_DMA_SUSPEND_READ(channel);
        G2_DMA_SUSPEND_WRITE(channel, 1);
    }

    while(FIFO_STATUS & (FIFO_SH4 | FIFO_G2));

    return ctx;
}

/** \brief  Enable IRQs and G2 DMA

    This function restores the IRQ and G2 DMA states using the context value
    generated by g2_lock().

    \param  ctx             The context containing IRQ and G2 DMA states.
*/
static inline void g2_unlock(g2_ctx_t ctx) {
    uint32_t channel;

    /* Preserve each DMA owner's pre-existing suspend state. */
    for(channel = 0; channel < 4; ++channel)
        G2_DMA_SUSPEND_WRITE(channel, ctx.dma_suspend[channel]);

    irq_restore(ctx.irq_state);
}

/** \cond */
static inline void __g2_scoped_cleanup(g2_ctx_t *state) {
    g2_unlock(*state);
}

#define ___g2_lock_scoped(l) \
    g2_ctx_t __scoped_g2_lock_##l __attribute__((cleanup(__g2_scoped_cleanup))) = g2_lock()

#define __g2_lock_scoped(l) ___g2_lock_scoped(l)
/** \endcond */

/** \brief  Disable IRQs and G2 DMA with scope management

    This function makes the following g2_read_*()/g2_write_*() functions atomic
    by disabling IRQs and G2 DMA and storing their states. The state will
    automatically be restored once the execution exits the functional block in
    which the macro was called.
*/
#define g2_lock_scoped() __g2_lock_scoped(__LINE__)

#ifdef __G2_DMA_SUSPEND_READ_LOCAL
#undef G2_DMA_SUSPEND_READ
#undef __G2_DMA_SUSPEND_READ_LOCAL
#endif
#ifdef __G2_DMA_SUSPEND_WRITE_LOCAL
#undef G2_DMA_SUSPEND_WRITE
#undef __G2_DMA_SUSPEND_WRITE_LOCAL
#endif
#undef G2_DMA_SUSPEND_ADDR

/** \brief  Read one byte from G2.

    This function reads a single byte from the specified address, taking all
    necessary precautions that are required for accessing G2.

    \param  address         The address in memory to read.
    \return                 The byte read from the address specified.
*/
uint8_t g2_read_8(uintptr_t address);

/** \brief  Write a single byte to G2.

    This function writes one byte to the specified address, taking all the
    necessary precautions to ensure your write actually succeeds.

    \param  address         The address in memory to write to.
    \param  value           The value to write to that address.
*/
void g2_write_8(uintptr_t address, uint8_t value);

/** \brief  Read one 16-bit word from G2.

    This function reads a single word from the specified address, taking all
    necessary precautions that are required for accessing G2.

    \param  address         The address in memory to read.
    \return                 The word read from the address specified.
*/
uint16_t g2_read_16(uintptr_t address);

/** \brief  Write a 16-bit word to G2.

    This function writes one word to the specified address, taking all the
    necessary precautions to ensure your write actually succeeds.

    \param  address         The address in memory to write to.
    \param  value           The value to write to that address.
*/
void g2_write_16(uintptr_t address, uint16_t value);

/** \brief  Read one 32-bit dword from G2.

    This function reads a single dword from the specified address, taking all
    necessary precautions that are required for accessing G2.

    \param  address         The address in memory to read.
    \return                 The dword read from the address specified.
*/
uint32_t g2_read_32(uintptr_t address);

/** \brief  Non-blocked read one 32-bit dword from G2.

    This function reads a single dword from the specified address, without all
    necessary precautions that are required for accessing G2.

    \param  address         The address in memory to read.
    \return                 The dword read from the address specified.
*/
static inline uint32_t g2_read_32_raw(uintptr_t address) {
    return *((volatile uint32_t *)address);
}

/** \brief  Write a 32-bit dword to G2.

    This function writes one dword to the specified address, taking all the
    necessary precautions to ensure your write actually succeeds.

    \param  address         The address in memory to write to.
    \param  value           The value to write to that address.
*/
void g2_write_32(uintptr_t address, uint32_t value);

/** \brief  Non-blocked write a 32-bit dword to G2.

    This function writes one dword to the specified address, without all the
    necessary precautions to ensure your write actually succeeds.

    \param  address         The address in memory to write to.
    \param  value           The value to write to that address.
*/
static inline void g2_write_32_raw(uintptr_t address, uint32_t value) {
    *((volatile uint32_t *)address) = value;
}

/** \brief  Read a block of bytes from G2.

    This function acts as memcpy() for copying data from G2 to system memory. It
    will take the necessary precautions before accessing G2 for you as well.

    \param  output          Pointer in system memory to write to.
    \param  address         The address in G2-space to read from.
    \param  amt             The number of bytes to read.
*/
void g2_read_block_8(uint8_t * output, uintptr_t address, size_t amt);

/** \brief  Write a block of bytes to G2.

    This function acts as memcpy() for copying data to G2 from system memory. It
    will take the necessary precautions for accessing G2.

    \param  input           The pointer in system memory to read from.
    \param  address         The address in G2-space to write to.
    \param  amt             The number of bytes to write.
*/
void g2_write_block_8(const uint8_t * input, uintptr_t address, size_t amt);

/** \brief  Read a block of words from G2.

    This function acts as memcpy() for copying data from G2 to system memory,
    but it copies 16 bits at a time. It will take the necessary precautions
    before accessing G2 for you as well.

    \param  output          Pointer in system memory to write to.
    \param  address         The address in G2-space to read from.
    \param  amt             The number of words to read.
*/
void g2_read_block_16(uint16_t * output, uintptr_t address, size_t amt);

/** \brief  Write a block of words to G2.

    This function acts as memcpy() for copying data to G2 from system memory,
    copying 16 bits at a time. It will take the necessary precautions for
    accessing G2.

    \param  input           The pointer in system memory to read from.
    \param  address         The address in G2-space to write to.
    \param  amt             The number of words to write.
*/
void g2_write_block_16(const uint16_t * input, uintptr_t address, size_t amt);

/** \brief  Read a block of dwords from G2.

    This function acts as memcpy() for copying data from G2 to system memory,
    but it copies 32 bits at a time. It will take the necessary precautions
    before accessing G2 for you as well.

    \param  output          Pointer in system memory to write to.
    \param  address         The address in G2-space to read from.
    \param  amt             The number of dwords to read.
*/
void g2_read_block_32(uint32_t * output, uintptr_t address, size_t amt);

/** \brief  Write a block of dwords to G2.

    This function acts as memcpy() for copying data to G2 from system memory,
    copying 32 bits at a time. It will take the necessary precautions for
    accessing G2.

    \param  input           The pointer in system memory to read from.
    \param  address         The address in G2-space to write to.
    \param  amt             The number of dwords to write.
*/
void g2_write_block_32(const uint32_t * input, uintptr_t address, size_t amt);

/** \brief  Set a block of bytes to G2.

    This function acts as memset() for setting a block of bytes on G2. It will
    take the necessary precautions for accessing G2.

    \param  address         The address in G2-space to write to.
    \param  c               The byte to write.
    \param  amt             The number of bytes to write.
*/
void g2_memset_8(uintptr_t address, uint8_t c, size_t amt);

/** \brief  Wait for the G2 write FIFO to empty.

    This function will spinwait until the G2 FIFO indicates that it has been
    drained. The FIFO is 32 bytes in length, and thus when accessing AICA you
    must do this at least for every 8 32-bit writes that you execute.
*/
void g2_fifo_wait(void);

/** @} */

__END_DECLS

#endif  /* __DC_G2BUS_H */
