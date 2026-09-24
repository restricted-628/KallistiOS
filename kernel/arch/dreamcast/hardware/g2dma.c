/* KallistiOS ##version##

   g2dma.c
   Copyright (C) 2001, 2002, 2004 Megan Potter
   Copyright (C) 2023 Andy Barajas
   Copyright (C) 2024 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <dc/asic.h>
#include <dc/g2bus.h>
#include <dc/memory.h>
#include <kos/cache.h>
#include <kos/dbglog.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>


typedef struct {
    uint32_t      g2_addr;        /* G2 Bus start address */
    uint32_t      sh4_addr;       /* SH-4 start address */
    uint32_t      size;           /* Size in bytes; Must be 32-byte aligned */
    uint32_t      dir;            /* 0: sh4->g2bus; 1: g2bus->sh4 */
    uint32_t      trigger_select; /* DMA trigger select; 0-CPU, 1-HW, 2-I */
    uint32_t      enable;         /* DMA enable */
    uint32_t      start;          /* DMA start */
    uint32_t      suspend;        /* DMA suspend */
} g2_dma_ctrl_t;

typedef struct {
    g2_dma_ctrl_t dma[4];
    uint32_t      g2_id;         /* G2 ID Bus version (Read only) */
    uint32_t      u1[3];         /* Unused */
    uint32_t      ds_timeout;    /* G2 DS timeout in clocks (default: 0x3ff) */
    uint32_t      tr_timeout;    /* G2 TR timeout in clocks (default: 0x3ff) */
    uint32_t      modem_timeout; /* G2 Modem timeout in cycles */
    uint32_t      modem_wait;    /* G2 Modem wait time in cycles */
    uint32_t      u2[7];         /* Unused */
    uint32_t      protection;    /* System memory area protection range */
} g2_dma_reg_t;

/* Each physical channel has one serialized operation. Terminal result and
   sequence fields remain available after the hardware stops, while the
   semaphore is only a wakeup token for the one active waiter. */
static semaphore_t dma_done[4];
static int dma_progress[4];
/* One reserved wait result survives callback chaining and is not overwritten
   by later transfers. The semaphore belongs to that waiter, not the channel's
   most recent transfer. */
static bool dma_waiting[4];
static bool dma_wait_ready[4];
static uint64_t dma_wait_sequence[4];
static int dma_wait_result[4];
static g2_dma_callback_t dma_callback[4];
static void *dma_cbdata[4];
static g2_dma_state_t dma_state[4];
static size_t dma_requested[4];
static uint64_t dma_sequence[4];
static uint64_t dma_completions[4];
static uint64_t dma_cancellations[4];
static int dma_result[4];
static bool dma_callback_pending[4];
static asic_evt_claim_t dma_irq_claim[4];
static uintptr_t dma_root_address[4];
static bool dma_root_cacheable[4];
static g2_dma_root_region_t dma_root_region[4];
static uint32_t dma_direction[4];

static bool dma_init;
static bool dma_lifecycle_busy;

/* G2 Bus DMA registers */
#ifndef G2_DMA_REG_BASE
#define G2_DMA_REG_BASE 0xa05f7800
#endif
static volatile g2_dma_reg_t * const g2_dma = (g2_dma_reg_t *)G2_DMA_REG_BASE;

/* Testable MMIO boundaries. A stop write is not an idle acknowledgement. */
#ifndef G2_DMA_START_READ
#define G2_DMA_START_READ(channel) (g2_dma->dma[channel].start)
#endif
#ifndef G2_DMA_START_WRITE
#define G2_DMA_START_WRITE(channel, value) (g2_dma->dma[channel].start = (value))
#endif
#ifndef G2_DMA_ACK
#define G2_DMA_ACK(channel) \
    (*(volatile uint32_t *)0xa05f6900 = UINT32_C(1) << (15u + (channel)))
#endif

#define DMA_SIZE_MASK 0x7fffffffu
#define VRAM_64_PHYSICAL_BASE 0x04000000u
#define VRAM_32_PHYSICAL_BASE 0x05000000u
#define VRAM_RETAIL_SIZE      0x00800000u
#define VRAM_DEVELOP_SIZE     0x01000000u

static bool dma_root_alias_valid(uintptr_t address) {
    uintptr_t area = address & ~MEM_AREA_CACHE_MASK;

    switch(area) {
        case MEM_AREA_P0_BASE:
        case MEM_AREA_P3_BASE:
            /*
                P0/P3 addresses may be translated and non-contiguous while
                the MMU is active. G2 DMA accepts only one physical span, so
                masking those virtual addresses would target unrelated RAM.
            */
            return !mmu_enabled();
        case MEM_AREA_P1_BASE:
        case MEM_AREA_P2_BASE:
            return true;
        default:
            return false;
    }
}

static bool dma_range_valid(uintptr_t address, size_t length,
                            uintptr_t lower, uintptr_t upper) {
    return address >= lower && address < upper && length <= upper - address;
}

static g2_dma_root_region_t dma_root_range_classify(const void *address,
                                                     size_t length) {
    uintptr_t virtual_address = (uintptr_t)address;
    uintptr_t physical_address = virtual_address & MEM_AREA_CACHE_MASK;
    uintptr_t vram_size = hardware_sys_mode(NULL) == HW_TYPE_RETAIL
        ? VRAM_RETAIL_SIZE : VRAM_DEVELOP_SIZE;

    if(!dma_root_alias_valid(virtual_address))
        return G2_DMA_ROOT_INVALID;
    if((HW_MEMSIZE == UINT32_C(0x01000000)
        || HW_MEMSIZE == UINT32_C(0x02000000))
       && dma_range_valid(physical_address, length, UINT32_C(0x0c000000),
                          UINT32_C(0x0c000000) + HW_MEMSIZE))
        return G2_DMA_ROOT_SYSTEM_RAM;
    if(dma_range_valid(physical_address, length,
                       VRAM_64_PHYSICAL_BASE,
                       VRAM_64_PHYSICAL_BASE + vram_size))
        return G2_DMA_ROOT_VRAM_64;
    if(dma_range_valid(physical_address, length,
                       VRAM_32_PHYSICAL_BASE,
                       VRAM_32_PHYSICAL_BASE + vram_size))
        return G2_DMA_ROOT_VRAM_32;
    return G2_DMA_ROOT_INVALID;
}

static bool dma_g2_range_valid(const void *address, size_t length) {
    uintptr_t virtual_address = (uintptr_t)address;
    uintptr_t physical_address = virtual_address & MEM_AREA_CACHE_MASK;
    uintptr_t area = virtual_address & ~MEM_AREA_CACHE_MASK;
    uintptr_t bus_area = physical_address >> 26;

    /* A G2 endpoint is a bus address, not an SH-4 virtual mapping. Physical
       addresses and the ordinary P1/P2/P3 aliases all normalize through the
       controller's 29-bit address field even while the MMU is active. */
    return (area == MEM_AREA_P0_BASE || area == MEM_AREA_P1_BASE
            || area == MEM_AREA_P2_BASE || area == MEM_AREA_P3_BASE)
        && (bus_area == 0 || bus_area == 5)
        && length <= ((bus_area + 1) << 26) - physical_address;
}


/*
    List of possible initiation triggers values to assign to trigger_select:
        CPU_TRIGGER: Software-driven. (Setting enable and start to 1)
        HARDWARE_TRIGGER: Via AICA (DMA0) or expansion device.
        INTERRUPT_TRIGGER: Based on interrupt signals.
*/
#define CPU_TRIGGER       0
#define HARDWARE_TRIGGER  1
#define INTERRUPT_TRIGGER 2

/*
    Controls whether the DMA suspend register of a channel is enabled:
        0x00000004: Enables the suspend register.
        0x00000000: Disables the suspend register.

    OR '|' this value with trigger when setting the trigger select of the
    DMA channel.
*/
#define DMA_SUSPEND_ENABLED    0x00000004
#define DMA_SUSPEND_DISABLED   0x00000000

/*
    For sh4 and g2bus addresses, ensure bits 31-29 & 4-0 are '0' to avoid
    illegal interrupts. Only bits 28-5 are used for valid addresses.
*/
#define MASK_ADDRESS      0x1fffffe0

/*
    Controls DMA initiation behavior after a DMA transfer completes:
        0x00000000: Preserve the current DMA enable setting.
        0x80000000: Reset the DMA enable setting to "0" after transfer.

    OR '|' this value with length when setting the size of the DMA request.
*/
#define PRESERVE_ENABLED  0x00000000
#define RESET_ENABLED     0x80000000

/*
    Specifies system memory address range for G2-DMA across channels 0-3.
    If a DMA transfer is generated outside of this range, an overrun error
    occurs.

    Previous range (0x4659404f):
        0x0C400000 - 0x0C4F0000

    Current range (0x4659007F):
        0x08000000 - 0x0FFFFFFF (Effectively disabling mem protection)
        Software admission independently limits system RAM to HW_MEMSIZE.

    How its calculated:

    xxxx xxxx xxxx xxxx ---- ---- ---- ---- : (0x4659)
    ---- ---- ---- ---- -xxx xxxx ---- ---- : Top range
    ---- ---- ---- ---- ---- ---- -xxx xxxx : Bottom range

    top_range = (value & 0x7f00) >> 8;
    bottom_range = (value & 0x7f);

    top_addr = (top_range << 20) | 0x08000000;
    bottom_addr = (bottom_range << 20) | 0x080fffff;
*/
#define SYS_MEM_SECURITY_CODE 0x4659
#define DISABLE_SYS_MEM_PROTECTION (SYS_MEM_SECURITY_CODE << 16 | 0x007F)
#define ENABLE_SYS_MEM_PROTECTION  (SYS_MEM_SECURITY_CODE << 16 | 0x7F00)

/*
    Set the DS# (Data Strobe) timeout to 27 clock cycles for the external DMA.
    If data isn't ready for latching by this time, an interrupt will be 
    triggered. 

    Not sure why its 27 but can be changed later. Default value
    is 1023 cycles (0x3ff).
*/
#define DS_CYCLE_OVERRIDE  27

/* Disable the DMA */
inline static bool dma_disable(uint32_t chn) {
    g2_dma->dma[chn].enable = 0;
    G2_DMA_START_WRITE(chn, 0);
    return !(G2_DMA_START_READ(chn) & 1u);
}

/* Called with IRQs fenced. Only the matching generation may signal a waiter. */
static void dma_publish_wait(uint32_t channel, int result) {
    if(dma_waiting[channel] && !dma_wait_ready[channel]
       && dma_wait_sequence[channel] == dma_sequence[channel]) {
        dma_wait_result[channel] = result;
        dma_wait_ready[channel] = true;
        sem_signal(&dma_done[channel]);
        if(irq_inside_int()) thd_schedule(true);
    }
}

static void dma_reserve_wait(uint32_t channel) {
    while(sem_count(&dma_done[channel]) > 0)
        (void)sem_trywait(&dma_done[channel]);
    dma_waiting[channel] = true;
    dma_wait_ready[channel] = false;
    dma_wait_sequence[channel] = dma_sequence[channel];
}

/* A timeout releases only the wait role, never hardware/buffer ownership. */
static int dma_finish_wait(uint32_t channel, uint32_t timeout) {
    uint64_t deadline = timeout ? timer_ms_gettime64() + timeout : 0;
    int wait_error = 0;
    for(;;) {
        uint32_t remaining = 0;
        irq_mask_t old = irq_disable();
        if(dma_wait_ready[channel]) {
            int result = dma_wait_result[channel];
            dma_waiting[channel] = false;
            irq_restore(old);
            if(result) { errno = result; return -1; }
            return 0;
        }
        if(deadline) {
            uint64_t now = timer_ms_gettime64();
            if(now >= deadline) wait_error = ETIMEDOUT;
            else remaining = (uint32_t)(deadline - now);
        }
        if(wait_error) {
            dma_waiting[channel] = false;
            irq_restore(old);
            errno = wait_error;
            return -1;
        }
        irq_restore(old);
        if(sem_wait_timed(&dma_done[channel], remaining) < 0)
            wait_error = errno;
    }
}

static void g2_dma_irq_hnd(uint32_t code, void *data) {
    int chn = code - ASIC_EVT_G2_DMA0;
    g2_dma_callback_t callback;
    void *callback_data;
    uint64_t sequence;

    (void)data;

    if(chn < G2_DMA_CHAN_SPU || chn > G2_DMA_CHAN_CH3) {
        dbglog(DBG_ERROR, "g2_dma: Wrong channel received in g2_dma_irq_hnd");
        return;
    }

    /* VP : changed the order of things so that we can chain dma calls */

    /* A late interrupt from a cancelled generation cannot complete a newly
       started engine that is still busy. */
    if(dma_progress[chn] && !(G2_DMA_START_READ(chn) & 1u)) {
        callback = dma_callback[chn];
        callback_data = dma_cbdata[chn];
        sequence = dma_sequence[chn];

        /* Publish device-to-memory writes only after the engine is terminal. */
        if(dma_direction[chn] == G2_DMA_TO_SH4
                && dma_root_cacheable[chn]) {
            dcache_inval_range(dma_root_address[chn], dma_requested[chn]);
        }

        dma_progress[chn] = 0;
        dma_callback[chn] = NULL;
        dma_cbdata[chn] = NULL;
        dma_state[chn] = G2_DMA_STATE_COMPLETE;
        dma_result[chn] = 0;
        ++dma_completions[chn];


        dma_publish_wait(chn, 0);

        /* Call the callback, if any. */
        if(callback)
            callback(callback_data);

        /* A completion callback may have chained a new transfer. */
        if(dma_sequence[chn] == sequence)
            dma_callback_pending[chn] = false;
    }
}

int g2_dma_transfer(void *sh4, void *g2bus, size_t length, uint32_t block,
                    g2_dma_callback_t callback, void *cbdata,
                    uint32_t dir, uint32_t mode, uint32_t g2chn, uint32_t sh4chn) {
    uint64_t sequence;
    g2_dma_root_region_t root_region;

    /* No longer used but we keep then around for compatibility */
    (void)mode;
    (void)sh4chn;

    if(!dma_init) {
        errno = ENODEV;
        return -1;
    }

    if(g2chn > G2_DMA_CHAN_CH3 || dir > G2_DMA_TO_SH4) {
        errno = EINVAL;
        return -1;
    }

    if(!length || (length & 31u) || length > DMA_SIZE_MASK) {
        errno = EINVAL;
        return -1;
    }

    if(block && irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    /* Check alignments */
    if(!sh4 || !__is_aligned(sh4, 32)) {
        dbglog(DBG_ERROR, "g2_dma: Unaligned sh4 DMA %p\n", sh4);
        errno = EFAULT;
        return -1;
    }

    if(!g2bus || !__is_aligned(g2bus, 32)) {
        dbglog(DBG_ERROR, "g2_dma: Unaligned g2bus DMA %p\n", g2bus);
        errno = EFAULT;
        return -1;
    }

    root_region = dma_root_range_classify(sh4, length);
    if(root_region == G2_DMA_ROOT_INVALID
            || !dma_g2_range_valid(g2bus, length)) {
        errno = EFAULT;
        return -1;
    }

    irq_disable_scoped();

    /* Close the admission race with shutdown after argument validation. */
    if(!dma_init) {
        errno = ENODEV;
        return -1;
    }

    /* Make sure we're not already DMA'ing */
    if(dma_progress[g2chn] != 0 || (G2_DMA_START_READ(g2chn) & 1u)) {
        errno = EINPROGRESS;
        return -1;
    }

    if(block && dma_waiting[g2chn]) {
        errno = EBUSY;
        return -1;
    }

    dma_progress[g2chn] = 1;

    dma_callback[g2chn] = callback;
    dma_cbdata[g2chn] = cbdata;
    dma_requested[g2chn] = length;
    dma_state[g2chn] = G2_DMA_STATE_RUNNING;
    dma_result[g2chn] = EINPROGRESS;
    dma_callback_pending[g2chn] = callback != NULL;
    dma_root_address[g2chn] = (uintptr_t)sh4;
    dma_root_region[g2chn] = root_region;
    dma_root_cacheable[g2chn] = root_region == G2_DMA_ROOT_SYSTEM_RAM
        && (((uintptr_t)sh4 & ~MEM_AREA_CACHE_MASK) != MEM_AREA_P2_BASE);
    dma_direction[g2chn] = dir;
    sequence = ++dma_sequence[g2chn];

    if(!sequence)
        sequence = ++dma_sequence[g2chn];
    if(block) dma_reserve_wait(g2chn);

    if(dma_root_cacheable[g2chn]) {
        if(dir == G2_DMA_TO_G2)
            dcache_wback_range(dma_root_address[g2chn], length);
        else
            dcache_inval_range(dma_root_address[g2chn], length);
    }

    /* Set needed registers */
    g2_dma->dma[g2chn].g2_addr = (uint32_t)(uintptr_t)g2bus & MASK_ADDRESS;
    g2_dma->dma[g2chn].sh4_addr = (uint32_t)(uintptr_t)sh4 & MASK_ADDRESS;
    g2_dma->dma[g2chn].size = length | RESET_ENABLED;
    g2_dma->dma[g2chn].dir = dir;

    if(g2chn == G2_DMA_CHAN_SPU) {
        /* Wait until fifo is empty and start. */
        g2_dma->dma[g2chn].trigger_select = HARDWARE_TRIGGER | DMA_SUSPEND_ENABLED;
    }
    else {
        g2_dma->dma[g2chn].trigger_select = CPU_TRIGGER | DMA_SUSPEND_ENABLED;
    }

    /* Start the DMA transfer */
    G2_DMA_ACK(g2chn);
    g2_dma->dma[g2chn].suspend = 0;
    g2_dma->dma[g2chn].enable = 1;
    G2_DMA_START_WRITE(g2chn, 1);

    /* Wait for us to be signaled */
    if(block)
        return dma_finish_wait(g2chn, 0);

    return 0;
}

int g2_dma_get_status(uint32_t channel, g2_dma_status_t *status) {
    irq_mask_t irq_state;
    size_t remaining;

    if(status)
        *status = (g2_dma_status_t) { 0 };

    if(channel > G2_DMA_CHAN_CH3 || !status) {
        errno = EINVAL;
        return -1;
    }

    if(!dma_init) {
        errno = ENODEV;
        return -1;
    }

    irq_state = irq_disable();

    if(!dma_init) {
        irq_restore(irq_state);
        errno = ENODEV;
        return -1;
    }

    remaining = dma_progress[channel]
        ? (g2_dma->dma[channel].size & DMA_SIZE_MASK) : 0;

    *status = (g2_dma_status_t) {
        .channel = channel,
        .state = dma_state[channel],
        .requested_bytes = dma_requested[channel],
        .remaining_bytes = remaining,
        .sequence = dma_sequence[channel],
        .completions = dma_completions[channel],
        .cancellations = dma_cancellations[channel],
        .result = dma_result[channel],
        .callback_pending = dma_callback_pending[channel],
        .root_region = dma_root_region[channel]
    };
    irq_restore(irq_state);
    return 0;
}

int g2_dma_wait(uint32_t channel, uint32_t timeout) {
    irq_mask_t irq_state;
    g2_dma_state_t state;
    int result;

    if(channel > G2_DMA_CHAN_CH3) {
        errno = EINVAL;
        return -1;
    }

    if(!dma_init) {
        errno = ENODEV;
        return -1;
    }

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq_state = irq_disable();

    if(!dma_init) {
        irq_restore(irq_state);
        errno = ENODEV;
        return -1;
    }

    state = dma_state[channel];
    result = dma_result[channel];

    if((state == G2_DMA_STATE_RUNNING || state == G2_DMA_STATE_SUSPENDED)
            && dma_waiting[channel]) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }

    if(state == G2_DMA_STATE_RUNNING || state == G2_DMA_STATE_SUSPENDED)
        dma_reserve_wait(channel);

    irq_restore(irq_state);

    if(state == G2_DMA_STATE_IDLE) {
        errno = ENOENT;
        return -1;
    }

    if(state == G2_DMA_STATE_RUNNING || state == G2_DMA_STATE_SUSPENDED)
        return dma_finish_wait(channel, timeout);

    if(result) {
        errno = result;
        return -1;
    }

    return 0;
}

int g2_dma_suspend(uint32_t channel) {
    irq_mask_t irq_state;

    if(channel > G2_DMA_CHAN_CH3) {
        errno = EINVAL;
        return -1;
    }

    if(!dma_init) {
        errno = ENODEV;
        return -1;
    }

    irq_state = irq_disable();

    if(!dma_init) {
        irq_restore(irq_state);
        errno = ENODEV;
        return -1;
    }

    if(!dma_progress[channel] || dma_state[channel] != G2_DMA_STATE_RUNNING) {
        irq_restore(irq_state);
        errno = EALREADY;
        return -1;
    }

    g2_dma->dma[channel].suspend = 1;
    dma_state[channel] = G2_DMA_STATE_SUSPENDED;
    irq_restore(irq_state);
    return 0;
}

int g2_dma_resume(uint32_t channel) {
    irq_mask_t irq_state;

    if(channel > G2_DMA_CHAN_CH3) {
        errno = EINVAL;
        return -1;
    }

    if(!dma_init) {
        errno = ENODEV;
        return -1;
    }

    irq_state = irq_disable();

    if(!dma_init) {
        irq_restore(irq_state);
        errno = ENODEV;
        return -1;
    }

    if(!dma_progress[channel]
            || dma_state[channel] != G2_DMA_STATE_SUSPENDED) {
        irq_restore(irq_state);
        errno = EALREADY;
        return -1;
    }

    g2_dma->dma[channel].suspend = 0;
    dma_state[channel] = G2_DMA_STATE_RUNNING;
    irq_restore(irq_state);
    return 0;
}

int g2_dma_cancel(uint32_t channel) {
    irq_mask_t irq_state;

    if(channel > G2_DMA_CHAN_CH3) {
        errno = EINVAL;
        return -1;
    }

    if(!dma_init) {
        errno = ENODEV;
        return -1;
    }

    irq_state = irq_disable();

    if(!dma_init) {
        irq_restore(irq_state);
        errno = ENODEV;
        return -1;
    }

    if(!dma_progress[channel]) {
        irq_restore(irq_state);
        errno = EALREADY;
        return -1;
    }

    if(!dma_disable(channel)) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }
    G2_DMA_ACK(channel);

    if(dma_direction[channel] == G2_DMA_TO_SH4
            && dma_root_cacheable[channel]) {
        dcache_inval_range(dma_root_address[channel], dma_requested[channel]);
    }

    dma_progress[channel] = 0;
    dma_callback[channel] = NULL;
    dma_cbdata[channel] = NULL;
    dma_callback_pending[channel] = false;
    dma_state[channel] = G2_DMA_STATE_CANCELLED;
    dma_result[channel] = ECANCELED;
    ++dma_cancellations[channel];
    dma_publish_wait(channel, ECANCELED);
    irq_restore(irq_state);

    return 0;
}

int g2_dma_init(void) {
    irq_mask_t irq_state;
    int saved_errno;
    int i, j;
    int semaphores_initialized = 0;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq_state = irq_disable();

    if(dma_init) {
        irq_restore(irq_state);
        return 0;
    }

    if(dma_lifecycle_busy) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }

    for(i = 0; i < 4; ++i) {
        if(G2_DMA_START_READ(i) & 1u) {
            irq_restore(irq_state);
            errno = EBUSY;
            return -1;
        }
    }

    dma_lifecycle_busy = true;
    irq_restore(irq_state);

    for(i = 0; i < 4; i++) {
        /* Create an initially blocked semaphore */
        if(sem_init(&dma_done[i], 0) < 0)
            goto fail;
        ++semaphores_initialized;
        dma_progress[i] = 0;
        dma_waiting[i] = false;
        dma_wait_ready[i] = false;
        dma_wait_sequence[i] = 0;
        dma_wait_result[i] = 0;
        dma_callback[i] = NULL;
        dma_cbdata[i] = NULL;
        dma_state[i] = G2_DMA_STATE_IDLE;
        dma_requested[i] = 0;
        dma_sequence[i] = 0;
        dma_completions[i] = 0;
        dma_cancellations[i] = 0;
        dma_result[i] = 0;
        dma_callback_pending[i] = false;
        dma_irq_claim[i] = ASIC_EVT_CLAIM_INVALID;
        dma_root_address[i] = 0;
        dma_root_cacheable[i] = false;
        dma_root_region[i] = G2_DMA_ROOT_INVALID;
        dma_direction[i] = G2_DMA_TO_G2;

        /* Each channel owns its completion source for the DMA lifetime. */
        if(asic_evt_claim(ASIC_EVT_G2_DMA0 + i, ASIC_IRQB,
                          g2_dma_irq_hnd, NULL, &dma_irq_claim[i]) < 0)
            goto fail;
    }

    /* Setup the DMA transfer on the external side */
    g2_dma->ds_timeout = DS_CYCLE_OVERRIDE;
    g2_dma->protection = DISABLE_SYS_MEM_PROTECTION;
    irq_state = irq_disable();
    dma_init = true;
    dma_lifecycle_busy = false;
    irq_restore(irq_state);

    return 0;

fail:
    saved_errno = errno;

    for(j = 0; j < 4; ++j) {
        if(dma_irq_claim[j] != ASIC_EVT_CLAIM_INVALID) {
            (void)asic_evt_release(dma_irq_claim[j]);
            dma_irq_claim[j] = ASIC_EVT_CLAIM_INVALID;
        }
    }

    for(j = 0; j < semaphores_initialized && j < 4; ++j)
        sem_destroy(&dma_done[j]);

    irq_state = irq_disable();
    dma_init = false;
    dma_lifecycle_busy = false;
    irq_restore(irq_state);
    errno = saved_errno;
    return -1;
}

void g2_dma_shutdown(void) {
    irq_mask_t irq_state;
    int i;

    if(irq_inside_int()) {
        errno = EPERM;
        return;
    }

    irq_state = irq_disable();

    if(dma_lifecycle_busy) {
        errno = EBUSY;
        goto out;
    }

    if(!dma_init)
        goto out;

    /* A signaled thread may not have consumed its result yet. Never destroy
       or reinitialize its semaphore/state underneath it. */
    for(i = 0; i < 4; ++i) {
        if(dma_waiting[i]) {
            errno = EBUSY;
            goto out;
        }
    }

    dma_lifecycle_busy = true;

    /* Stop active channels before detaching any IRQ or releasing ownership.
       Failure is retryable: leave the driver initialized and claims intact.
       Channels already stopped during this attempt remain cancelled. */
    for(i = 0; i < 4; ++i) {
        if(dma_progress[i]) {
            if(g2_dma_cancel(i) < 0) {
                dma_lifecycle_busy = false;
                goto out;
            }
        }
        else if(!dma_disable(i)) {
            dma_lifecycle_busy = false;
            errno = EBUSY;
            goto out;
        }
    }

    /* Detach every completion source while IRQ delivery is fenced. Otherwise
       a terminal interrupt can land between the active check and channel
       disable, invoke a callback, and then be published a second time as a
       shutdown cancellation. Keeping the fence through semaphore and
       protection teardown also prevents a concurrent reinitialization from
       observing or replacing a half-destroyed channel set. */
    dma_init = false;

    for(i = 0; i < 4; i++) {
        G2_DMA_ACK(i);
        /* Release the completion source owned by this channel. */
        if(dma_irq_claim[i] != ASIC_EVT_CLAIM_INVALID) {
            (void)asic_evt_release(dma_irq_claim[i]);
            dma_irq_claim[i] = ASIC_EVT_CLAIM_INVALID;
        }

        /* No waiter can still be using this semaphore. */
        sem_destroy(&dma_done[i]);
    }

    g2_dma->protection = ENABLE_SYS_MEM_PROTECTION;
    dma_lifecycle_busy = false;

out:
    irq_restore(irq_state);
}
