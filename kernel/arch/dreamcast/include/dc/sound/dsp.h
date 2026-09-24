/* KallistiOS ##version##

   dc/sound/dsp.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/sound/dsp.h
    \brief   Checked AICA DSP program and effect-output control.
    \ingroup audio_driver

    The DSP is one global hardware resource. This API owns its program,
    coefficient and address tables, sound-RAM work area, and sixteen effect
    returns. It does not create a thread, fiber, service, or periodic task.
    Channel input selection and send level use the routing fields in
    snd_channel_config_t and snd_channel_update().

    \author Joseph Black
*/

#ifndef __DC_SOUND_DSP_H
#define __DC_SOUND_DSP_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** \defgroup audio_dsp Checked DSP Control
    \brief AICA DSP programs, work memory, and effect returns
    \ingroup audio_driver

    @{ */

#define SND_DSP_COEFFICIENT_COUNT 128u /**< Coefficients per program. */
#define SND_DSP_ADDRESS_COUNT      64u /**< Sound-memory addresses. */
#define SND_DSP_STEP_COUNT        128u /**< Microprogram steps. */
#define SND_DSP_STEP_WORDS          4u /**< Register words per step. */
#define SND_DSP_OUTPUT_COUNT        16u /**< Effect-return channels. */
#define SND_DSP_MAX_WORK_BYTES  131072u /**< Maximum addressable work span. */

/** \brief DSP ring-buffer size selector. */
typedef enum snd_dsp_ring_size {
    SND_DSP_RING_16K = 0, /**< 16 KiB, or 8K 16-bit words. */
    SND_DSP_RING_32K = 1, /**< 32 KiB, or 16K 16-bit words. */
    SND_DSP_RING_64K = 2, /**< 64 KiB, or 32K 16-bit words. */
    SND_DSP_RING_128K = 3 /**< 128 KiB, or 64K 16-bit words. */
} snd_dsp_ring_size_t;

/** \brief One microprogram step in increasing hardware-address order. */
typedef struct snd_dsp_instruction {
    uint16_t word[SND_DSP_STEP_WORDS]; /**< Bits 63:48 through 15:0. */
} snd_dsp_instruction_t;

/** \brief Complete caller-owned DSP program image. */
typedef struct snd_dsp_program {
    uint16_t coefficients[SND_DSP_COEFFICIENT_COUNT]; /**< Raw 16-bit COEF registers. */
    uint16_t addresses[SND_DSP_ADDRESS_COUNT]; /**< Raw word-address registers. */
    snd_dsp_instruction_t steps[SND_DSP_STEP_COUNT]; /**< 128-step microprogram. */
    snd_dsp_ring_size_t ring_size; /**< Hardware ring-buffer geometry. */
    const void *work_initial_data; /**< Optional initial work-memory prefix. */
    size_t work_initial_bytes; /**< Bytes supplied by work_initial_data. */
} snd_dsp_program_t;

/** \brief Logical configuration for one DSP effect return. */
typedef struct snd_dsp_output {
    uint8_t level; /**< Output level, 0 silent through 15 maximum. */
    uint8_t pan;   /**< 0 left, 128 center, 255 right. */
} snd_dsp_output_t;

/** \brief Coherent driver-owned DSP state. */
typedef struct snd_dsp_status {
    bool loaded; /**< Whether a nonempty program is installed. */
    uint32_t generation; /**< Increments after every load or clear. */
    uint32_t work_address; /**< Aligned sound-RAM work-area offset. */
    size_t work_bytes; /**< Complete owned work-area span. */
    snd_dsp_ring_size_t ring_size; /**< Current ring-buffer size. */
    snd_dsp_output_t output[SND_DSP_OUTPUT_COUNT]; /**< Effect-return controls. */
} snd_dsp_status_t;

/** \brief Initialize an empty program description.

    The default ring size is \ref SND_DSP_RING_16K. The resulting all-zero
    microprogram is intentionally not loadable; use snd_dsp_program_clear()
    when the desired state is no DSP program.

    \param program         Program description to initialize.
    \retval 0              On success.
    \retval -1             If \p program is NULL, with errno set to EINVAL.
*/
int snd_dsp_program_init(snd_dsp_program_t *program);

/** \brief Validate a DSP image without touching hardware.

    Validation checks reserved instruction and coefficient bits, input-source
    selection, legal memory-access step placement, the declared ring geometry,
    initial work data, and the largest sound-memory address the program can
    reach. Table accesses which can wrap the 16-bit word address space
    conservatively require the full 128 KiB work area.

    \param program         Program to validate.
    \param required_work_bytes Optional result for required sound RAM.
    \retval 0              The image is structurally valid.
    \retval -1             The image is invalid, with errno set to EINVAL.
*/
int snd_dsp_program_validate(const snd_dsp_program_t *program,
                             size_t *required_work_bytes);

/** \brief Load one validated DSP program synchronously.

    KOS allocates and aligns an isolated sound-RAM work area, initializes it,
    then replaces the global DSP state. Allocation and validation finish before
    the old program is touched, so those failures leave it installed. This
    failure-atomic replacement temporarily requires storage for both work
    areas; clear the old program first when that tradeoff is undesirable.

    Program replacement is rejected with EBUSY while any playing channel has
    a nonzero DSP send. A caller that wants an audible transition must first
    set those sends to zero with snd_channel_update(), observe the updated
    channel snapshots, and retry. The command queue is held exclusively while
    the hardware program changes, preventing a new routed start from racing
    admission.

    \param program         Complete program image.
    \param timeout_ms      Nonzero deadline for command-queue drain.
    \retval 0              Program installed and work memory owned by KOS.
    \retval -1             On error with errno set to EINVAL, ENODEV, ENOMEM,
                           EBUSY, EAGAIN, EPROTO, EPERM, or ETIMEDOUT.
*/
int snd_dsp_program_load(const snd_dsp_program_t *program,
                         uint32_t timeout_ms);

/** \brief Stop and remove the current DSP program synchronously.

    As with loading, clearing returns EBUSY while a playing channel has a
    nonzero DSP send. On success the effect returns are muted and owned work
    memory is released.

    \param timeout_ms      Nonzero deadline for command-queue drain.
    \retval 0              DSP stopped; also succeeds when already clear.
    \retval -1             On error with errno set as for program loading.
*/
int snd_dsp_program_clear(uint32_t timeout_ms);

/** \brief Configure all sixteen effect returns atomically with respect to
           other DSP API calls.

    \param output          Array of exactly SND_DSP_OUTPUT_COUNT entries.
    \retval 0              Hardware outputs and coherent state updated.
    \retval -1             On error with errno set.
*/
int snd_dsp_outputs_set(
    const snd_dsp_output_t output[SND_DSP_OUTPUT_COUNT]);

/** \brief Configure one effect return.

    \param channel         Return channel, 0 through 15.
    \param output          New level and pan.
    \retval 0              Hardware output and coherent state updated.
    \retval -1             On error with errno set.
*/
int snd_dsp_output_set(unsigned int channel,
                       const snd_dsp_output_t *output);

/** \brief Read one effect-return configuration.

    \param channel         Return channel, 0 through 15.
    \param output          Receives the coherent level and pan.
    \retval 0              Configuration copied.
    \retval -1             On error with errno set.
*/
int snd_dsp_output_get(unsigned int channel, snd_dsp_output_t *output);

/** \brief Copy the complete coherent DSP state.

    \param status          Receives the current driver-owned state.
    \retval 0              State copied.
    \retval -1             On error with errno set.
*/
int snd_dsp_get_status(snd_dsp_status_t *status);

/** @} */

__END_DECLS

#endif /* __DC_SOUND_DSP_H */
