/* KallistiOS ##version##

   snd_dsp.c
   Copyright (C) 2026 Joseph Black

   Checked ownership of the AICA DSP and effect-return mixer.
*/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arch/arch.h>
#include <dc/g2bus.h>
#include <dc/memory.h>
#include <dc/sound/dsp.h>
#include <dc/sound/sound.h>
#include <dc/spu.h>
#include <kos/irq.h>
#include <kos/mutex.h>

#include "arm/aica_cmd_iface.h"
#include "snd_iface_internal.h"

#define DSP_REGISTER_BASE (MEM_AREA_P2_BASE | UINT32_C(0x00700000))
#define DSP_OUTPUT_BASE    UINT32_C(0x2000)
#define DSP_RING_CONTROL   UINT32_C(0x2804)
#define DSP_COEFFICIENT_BASE UINT32_C(0x3000)
#define DSP_ADDRESS_BASE   UINT32_C(0x3200)
#define DSP_PROGRAM_BASE   UINT32_C(0x3400)
#define DSP_TEMP_BASE      UINT32_C(0x4000)
#define DSP_MEMORY_BASE    UINT32_C(0x4400)
#define DSP_EFFECT_BASE    UINT32_C(0x4580)
#define DSP_RING_BASE_MASK UINT32_C(0x00000fff)
#define DSP_RING_SIZE_MASK UINT32_C(0x00006000)
#define DSP_RING_SIZE_SHIFT 13
#define DSP_WORK_ALIGNMENT UINT32_C(2048)

static mutex_t dsp_mutex = MUTEX_INITIALIZER;
static snd_dsp_status_t dsp_status;
static uint32_t dsp_allocation_base;

static uint32_t dsp_register(uint32_t offset) {
    return DSP_REGISTER_BASE + offset;
}

static uint8_t hardware_pan(uint8_t pan) {
    if(pan == 128)
        return 0;
    if(pan < 128)
        return (uint8_t)(UINT8_C(0x10) | ((127u - pan) >> 3));
    return (uint8_t)((pan - 128u) >> 3);
}

static void fifo_step(size_t *writes) {
    if(((*writes)++ & 7u) == 0)
        g2_fifo_wait();
}

static void write_outputs_raw(const snd_dsp_output_t *outputs) {
    size_t channel;
    size_t writes = 0;

    for(channel = 0; channel < SND_DSP_OUTPUT_COUNT; ++channel) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_OUTPUT_BASE + channel * 4u),
                        ((uint32_t)outputs[channel].level << 8) |
                        hardware_pan(outputs[channel].pan));
    }
}

static void mute_outputs_raw(void) {
    size_t channel;
    size_t writes = 0;

    for(channel = 0; channel < SND_DSP_OUTPUT_COUNT; ++channel) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_OUTPUT_BASE + channel * 4u), 0);
    }
}

static void clear_program_raw(void) {
    size_t word;
    size_t writes = 0;

    /* A fully zero microprogram is the hardware's stopped state. Clear it
       before changing any table so the old program cannot observe a mixture
       of old instructions and new coefficients. */
    for(word = 0; word < SND_DSP_STEP_COUNT * SND_DSP_STEP_WORDS; ++word) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_PROGRAM_BASE + word * 4u), 0);
    }
}

static void clear_runtime_raw(void) {
    size_t item;
    size_t writes = 0;

    for(item = 0; item < SND_DSP_COEFFICIENT_COUNT; ++item) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_COEFFICIENT_BASE + item * 4u), 0);
    }
    for(item = 0; item < SND_DSP_ADDRESS_COUNT; ++item) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_ADDRESS_BASE + item * 4u), 0);
    }

    /* TEMP and MEMS expose one low and one high register per 24-bit value.
       MIXS is intentionally omitted: the slot mixer owns it continuously. */
    for(item = 0; item < SND_DSP_STEP_COUNT; ++item) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_TEMP_BASE + item * 8u), 0);
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_TEMP_BASE + item * 8u + 4u), 0);
    }
    for(item = 0; item < 32u; ++item) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_MEMORY_BASE + item * 8u), 0);
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_MEMORY_BASE + item * 8u + 4u), 0);
    }
    for(item = 0; item < SND_DSP_OUTPUT_COUNT; ++item) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_EFFECT_BASE + item * 4u), 0);
    }
}

static void write_program_raw(const snd_dsp_program_t *program,
                              uint32_t work_address) {
    size_t item;
    size_t word;
    size_t writes = 0;
    uint32_t ring;

    for(item = 0; item < SND_DSP_COEFFICIENT_COUNT; ++item) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_COEFFICIENT_BASE + item * 4u),
                        program->coefficients[item]);
    }
    for(item = 0; item < SND_DSP_ADDRESS_COUNT; ++item) {
        fifo_step(&writes);
        g2_write_32_raw(dsp_register(DSP_ADDRESS_BASE + item * 4u),
                        program->addresses[item]);
    }

    fifo_step(&writes);
    ring = g2_read_32_raw(dsp_register(DSP_RING_CONTROL));
    ring &= ~(DSP_RING_BASE_MASK | DSP_RING_SIZE_MASK);
    ring |= (work_address >> 11) & DSP_RING_BASE_MASK;
    ring |= (uint32_t)program->ring_size << DSP_RING_SIZE_SHIFT;
    g2_write_32_raw(dsp_register(DSP_RING_CONTROL), ring);

    /* The DSP has no independent halt bit: the first nonzero program word
       begins execution. At this point all returns are muted, all sends are
       known idle, the tables are complete, and every reachable sound-memory
       address belongs to this program. Partial publication therefore cannot
       escape the isolated work area. */
    for(item = 0; item < SND_DSP_STEP_COUNT; ++item) {
        for(word = 0; word < SND_DSP_STEP_WORDS; ++word) {
            fifo_step(&writes);
            g2_write_32_raw(
                dsp_register(DSP_PROGRAM_BASE +
                             (item * SND_DSP_STEP_WORDS + word) * 4u),
                program->steps[item].word[word]);
        }
    }
}

static void hardware_load(const snd_dsp_program_t *program,
                          uint32_t work_address) {
    g2_lock_scoped();

    mute_outputs_raw();
    clear_program_raw();
    clear_runtime_raw();
    write_program_raw(program, work_address);
    write_outputs_raw(dsp_status.output);
}

static void hardware_clear(bool reset_ring) {
    g2_lock_scoped();
    uint32_t ring;

    mute_outputs_raw();
    clear_program_raw();
    clear_runtime_raw();
    if(reset_ring) {
        g2_fifo_wait();
        ring = g2_read_32_raw(dsp_register(DSP_RING_CONTROL));
        ring &= ~(DSP_RING_BASE_MASK | DSP_RING_SIZE_MASK);
        g2_write_32_raw(dsp_register(DSP_RING_CONTROL), ring);
    }
}

static int active_effect_send(void) {
    unsigned int channel;

    for(channel = 0; channel < 64; ++channel) {
        snd_channel_status_ex_t status;

        if(snd_channel_get_status_ex(channel, &status) < 0)
            return -1;
        if(status.playing && status.config.routing.effect_send) {
            errno = EBUSY;
            return 1;
        }
    }
    return 0;
}

static uint32_t next_generation(void) {
    uint32_t generation = dsp_status.generation + 1u;

    return generation ? generation : 1u;
}

int snd_dsp_program_load(const snd_dsp_program_t *program,
                         uint32_t timeout_ms) {
    size_t work_bytes;
    size_t allocation_bytes;
    uint32_t allocation_base;
    uint32_t work_address;
    uint32_t old_allocation;
    int send_status;

    if(!timeout_ms || irq_inside_int()) {
        errno = !timeout_ms ? EINVAL : EPERM;
        return -1;
    }
    if(snd_dsp_program_validate(program, &work_bytes) < 0)
        return -1;
    if(!snd_iface_is_initialized()) {
        errno = ENODEV;
        return -1;
    }
    if(mutex_lock_timed(&dsp_mutex, timeout_ms) < 0)
        return -1;

    allocation_bytes = work_bytes + DSP_WORK_ALIGNMENT - 1u;
    allocation_base = snd_mem_malloc(allocation_bytes);
    if(!allocation_base)
        goto fail_mutex;
    work_address = (allocation_base + DSP_WORK_ALIGNMENT - 1u) &
                   ~(DSP_WORK_ALIGNMENT - 1u);

    /* Prepare an unreferenced allocation before touching the running DSP.
       This makes allocation and initialization failure-independent from the
       currently installed program. */
    spu_memset(work_address, 0, work_bytes);
    if(program->work_initial_bytes)
        spu_memload(work_address, program->work_initial_data,
                    program->work_initial_bytes);

    if(snd_iface_exclusive_begin(timeout_ms) < 0)
        goto fail_allocation;
    send_status = active_effect_send();
    if(send_status != 0) {
        snd_iface_exclusive_end();
        goto fail_allocation;
    }

    hardware_load(program, work_address);
    old_allocation = dsp_allocation_base;
    dsp_allocation_base = allocation_base;
    dsp_status.loaded = true;
    dsp_status.generation = next_generation();
    dsp_status.work_address = work_address;
    dsp_status.work_bytes = work_bytes;
    dsp_status.ring_size = program->ring_size;
    snd_iface_exclusive_end();

    if(old_allocation)
        snd_mem_free(old_allocation);
    mutex_unlock(&dsp_mutex);
    return 0;

fail_allocation:
    snd_mem_free(allocation_base);
fail_mutex:
    mutex_unlock(&dsp_mutex);
    return -1;
}

int snd_dsp_program_clear(uint32_t timeout_ms) {
    uint32_t old_allocation;
    int send_status;

    if(!timeout_ms || irq_inside_int()) {
        errno = !timeout_ms ? EINVAL : EPERM;
        return -1;
    }
    if(!snd_iface_is_initialized()) {
        errno = ENODEV;
        return -1;
    }
    if(mutex_lock_timed(&dsp_mutex, timeout_ms) < 0)
        return -1;
    if(snd_iface_exclusive_begin(timeout_ms) < 0)
        goto fail_mutex;

    if(dsp_status.loaded) {
        send_status = active_effect_send();
        if(send_status != 0) {
            snd_iface_exclusive_end();
            goto fail_mutex;
        }
    }

    hardware_clear(true);
    old_allocation = dsp_allocation_base;
    dsp_allocation_base = 0;
    memset(&dsp_status.output, 0, sizeof(dsp_status.output));
    dsp_status.loaded = false;
    dsp_status.generation = next_generation();
    dsp_status.work_address = 0;
    dsp_status.work_bytes = 0;
    dsp_status.ring_size = SND_DSP_RING_16K;
    snd_iface_exclusive_end();

    if(old_allocation)
        snd_mem_free(old_allocation);
    mutex_unlock(&dsp_mutex);
    return 0;

fail_mutex:
    mutex_unlock(&dsp_mutex);
    return -1;
}

static int output_valid(const snd_dsp_output_t *output) {
    return output && output->level <= 15;
}

int snd_dsp_outputs_set(
    const snd_dsp_output_t output[SND_DSP_OUTPUT_COUNT]) {
    size_t channel;

    if(!output) {
        errno = EINVAL;
        return -1;
    }
    for(channel = 0; channel < SND_DSP_OUTPUT_COUNT; ++channel) {
        if(!output_valid(&output[channel])) {
            errno = EINVAL;
            return -1;
        }
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!snd_iface_is_initialized()) {
        errno = ENODEV;
        return -1;
    }
    if(mutex_lock(&dsp_mutex) < 0)
        return -1;
    memcpy(dsp_status.output, output, sizeof(dsp_status.output));
    {
        g2_lock_scoped();
        write_outputs_raw(dsp_status.output);
    }
    mutex_unlock(&dsp_mutex);
    return 0;
}

int snd_dsp_output_set(unsigned int channel,
                       const snd_dsp_output_t *output) {
    uint32_t value;

    if(channel >= SND_DSP_OUTPUT_COUNT || !output_valid(output)) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!snd_iface_is_initialized()) {
        errno = ENODEV;
        return -1;
    }
    if(mutex_lock(&dsp_mutex) < 0)
        return -1;
    dsp_status.output[channel] = *output;
    value = ((uint32_t)output->level << 8) | hardware_pan(output->pan);
    g2_write_32(dsp_register(DSP_OUTPUT_BASE + channel * 4u), value);
    mutex_unlock(&dsp_mutex);
    return 0;
}

int snd_dsp_output_get(unsigned int channel, snd_dsp_output_t *output) {
    if(channel >= SND_DSP_OUTPUT_COUNT || !output) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!snd_iface_is_initialized()) {
        errno = ENODEV;
        return -1;
    }
    if(mutex_lock(&dsp_mutex) < 0)
        return -1;
    *output = dsp_status.output[channel];
    mutex_unlock(&dsp_mutex);
    return 0;
}

int snd_dsp_get_status(snd_dsp_status_t *status) {
    if(!status) {
        errno = EINVAL;
        return -1;
    }
    memset(status, 0, sizeof(*status));
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!snd_iface_is_initialized()) {
        errno = ENODEV;
        return -1;
    }
    if(mutex_lock(&dsp_mutex) < 0)
        return -1;
    *status = dsp_status;
    mutex_unlock(&dsp_mutex);
    return 0;
}

void snd_dsp_system_reset(void) {
    if(mutex_lock(&dsp_mutex) < 0)
        return;
    dsp_allocation_base = 0;
    memset(&dsp_status, 0, sizeof(dsp_status));
    dsp_status.ring_size = SND_DSP_RING_16K;
    hardware_clear(true);
    mutex_unlock(&dsp_mutex);
}

void snd_dsp_system_shutdown(void) {
    uint32_t old_allocation;

    if(mutex_lock(&dsp_mutex) < 0)
        return;
    hardware_clear(true);
    old_allocation = dsp_allocation_base;
    dsp_allocation_base = 0;
    memset(&dsp_status, 0, sizeof(dsp_status));
    dsp_status.ring_size = SND_DSP_RING_16K;
    mutex_unlock(&dsp_mutex);

    if(old_allocation)
        snd_mem_free(old_allocation);
}
