/* KallistiOS ##version##

   asic.c
   Copyright (c)2000,2001,2002,2003 Megan Potter
   Copyright (C) 2026 Joseph Black
*/

/*
   This module contains low-level ASIC handling. Right now this is just for
   ASIC interrupts, but it will eventually include DMA as well.

   The DC's System ASIC is integrated with the 3D chip and serves as the
   Grand Central Station for the interaction of all the various peripherals
   (which really means that it allows the SH-4 to control all of them =).
   The basic block diagram looks like this:

   +-----------+    +--------+    +-----------------+
   | 16MB Ram  |    |        |----| 8MB Texture Ram |
   +-----------+    | System |    +-----------------+
      |             |  ASIC  |    +--------------------+  +-------------+
      +-------------+        +-+--+    AICA SPU        |--+ 2MB SPU RAM |
      |A            | PVR2DC | |  +-------------------++  +-------------+
   +-------+        |        | |C +-----------------+ |
   | SH-4  |        |        | \--+ Expansion Port  | |
   +-------+        +---+----+    +-----------------+ |
                        |B        +------------+      |D
                        +---------+   GD-Rom   +------/
                        |         +------------+
                        |         +----------------------+
                        \---------+ 2MB ROM + 256K Flash |
                                  +----------------------+

   A: Main system bus -- connects the SH-4 to the ASIC and its main RAM
   B: "G1" bus -- connects the ASIC to the GD-Rom and the ROM/Flash
   C: "G2" bus -- connects the ASIC to the SPU and Expansion port
   D: Not entirely verified connection for streaming audio data

   All buses can simultaneously transmit data via PIO and/or DMA. This
   is where the ridiculous bandwidth figures come from that you see in
   marketing literature. In reality, each bus isn't terribly fast on
   its own -- they tend to have high bandwidth but high latency.

   The "G2" bus is notoriously flaky. Specifically, one should ensure
   to write the proper data size for the peripheral you are accessing
   (32-bits for SPU, 8-bits for 8-bit peripherals, etc). Every 8
   32-bit words written to the SPU must be followed by a g2_fifo_wait().
   Additionally, if SPU or Expansion Port DMA is being used, only one
   of these may proceed at once and any PIO access _must_ pause the
   DMA and disable interrupts. Any other treatment may cause serious
   data corruption between the ASIC and the G2 peripherals.

 */

/* Small interrupt listing (from the two Marcus's =)

  691x -> irq 13
  692x -> irq 11
  693x -> irq 9

  69x0
    bit 2   render complete
        3   scanline 1
        4   scanline 2
        5   vsync
        7   opaque polys accepted
        8   opaque modifiers accepted
        9   translucent polys accepted
        10  translucent modifiers accepted
        12  maple dma complete
        13  maple error(?)
        14  gd-rom dma complete
        15  aica dma complete
        16  external dma 1 complete
        17  external dma 2 complete
        18  ??? dma complete
        21  punch-thru polys accepted

  69x4
    bit 0   gd-rom command status
        1   AICA
        2       modem?
        3       expansion port (PCI bridge)

  69x8
    bit 2   out of primitive memory
        3   out of matrix memory
        12  gd-rom dma illegal address
        13  gd-rom dma overrun

 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <dc/asic.h>
#include <kos/genwait.h>
#include <kos/irq.h>
#include <kos/regfield.h>
#include <kos/worker_thread.h>

/* Keep the register access in one overridable boundary so the ownership state
   machine can be exercised by the host regression harness. Production builds
   use the direct SH-4 MMIO definitions below. */
#ifndef ASIC_MMIO_READ32
#define ASIC_MMIO_READ32(addr) (*((volatile uint32_t *)(addr)))
#endif
#ifndef ASIC_MMIO_WRITE32
#define ASIC_MMIO_WRITE32(addr, data) \
    (*((volatile uint32_t *)(addr)) = (data))
#endif

#define IN32(addr)         ASIC_MMIO_READ32(addr)
#define OUT32(addr, data)  ASIC_MMIO_WRITE32(addr, data)

/* The set of asic regs are spaced by 0x10 with 0x4 between each sub reg */
#define ASIC_EVT_REG_ADDR(irq, sub) (ASIC_IRQD_A + ((irq) * 0x10) + ((sub) * 0x4))

#define ASIC_EVT_REGS 3
#define ASIC_EVT_REG_HNDS 32

struct asic_thdata {
    asic_evt_handler hdl;
    uint32_t source;
    kthread_worker_t *worker;
    void *data;
    void (*ack_and_mask)(uint16_t);
    void (*unmask)(uint16_t);
};

/* Exception table -- this table matches each potential G2 event to a function
   pointer. If the pointer is null, then nothing happens. Otherwise, the
   function will handle the exception. */
static asic_evt_handler_entry_t
asic_evt_handlers[ASIC_EVT_REGS][ASIC_EVT_REG_HNDS];
static uint16_t asic_evt_claim_generation[ASIC_EVT_REGS][ASIC_EVT_REG_HNDS];
static uint8_t asic_evt_claim_level[ASIC_EVT_REGS][ASIC_EVT_REG_HNDS];
static bool asic_evt_claim_active[ASIC_EVT_REGS][ASIC_EVT_REG_HNDS];
static uint64_t asic_evt_dispatches[ASIC_EVT_REGS][ASIC_EVT_REG_HNDS];
static struct asic_thdata
    *asic_evt_threaded[ASIC_EVT_REGS][ASIC_EVT_REG_HNDS];

static bool asic_evt_code_valid(uint16_t code) {
    return ((code >> 8) & 0xff) < ASIC_EVT_REGS
        && (code & 0xff) < ASIC_EVT_REG_HNDS;
}

static asic_evt_claim_t asic_evt_make_claim(uint16_t code,
                                            uint16_t generation) {
    return ((uint32_t)generation << 16) | code;
}

static bool asic_evt_claim_valid(asic_evt_claim_t claim,
                                 uint8_t *evtreg, uint8_t *evt) {
    uint16_t code = (uint16_t)claim;
    uint16_t generation = (uint16_t)(claim >> 16);

    if(!claim || !generation || !asic_evt_code_valid(code))
        return false;

    *evtreg = (uint8_t)(code >> 8);
    *evt = (uint8_t)code;
    return asic_evt_claim_active[*evtreg][*evt]
        && asic_evt_claim_generation[*evtreg][*evt] == generation;
}

/* Set a handler, or remove a handler */
asic_evt_handler_entry_t asic_evt_set_handler(uint16_t code, asic_evt_handler hnd, void *data) {
    uint8_t evtreg, evt;
    asic_evt_handler_entry_t old;
    irq_mask_t irq_state;

    if(!asic_evt_code_valid(code)) {
        errno = EINVAL;
        return (asic_evt_handler_entry_t) { NULL, NULL };
    }

    evtreg = (code >> 8) & 0xff;
    evt = code & 0xff;
    irq_state = irq_disable();

    old = asic_evt_handlers[evtreg][evt];

    /* An exclusive owner must use its claim token to release the event. */
    if(asic_evt_claim_active[evtreg][evt]
            || (asic_evt_threaded[evtreg][evt]
                && (hnd != old.hdl || data != old.data))) {
        irq_restore(irq_state);
        errno = EBUSY;
        return old;
    }

    asic_evt_handlers[evtreg][evt] = (asic_evt_handler_entry_t){ hnd, data };
    irq_restore(irq_state);
    return old;
}

int asic_evt_claim(uint16_t code, uint8_t irqlevel,
                   asic_evt_handler handler, void *data,
                   asic_evt_claim_t *claim) {
    irq_mask_t irq_state;
    uint16_t generation;
    uint8_t evtreg, evt, level;
    uint32_t addr;

    if(!claim) {
        errno = EINVAL;
        return -1;
    }

    *claim = ASIC_EVT_CLAIM_INVALID;

    if(!asic_evt_code_valid(code) || irqlevel >= ASIC_IRQ_MAX || !handler) {
        errno = EINVAL;
        return -1;
    }

    evtreg = (uint8_t)(code >> 8);
    evt = (uint8_t)code;
    irq_state = irq_disable();

    for(level = 0; level < ASIC_IRQ_MAX; ++level) {
        addr = ASIC_EVT_REG_ADDR(level, evtreg);
        if(IN32(addr) & BIT(evt)) {
            irq_restore(irq_state);
            errno = EBUSY;
            return -1;
        }
    }

    if(asic_evt_handlers[evtreg][evt].hdl
            || asic_evt_claim_active[evtreg][evt]) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }

    generation = ++asic_evt_claim_generation[evtreg][evt];

    if(!generation)
        generation = ++asic_evt_claim_generation[evtreg][evt];

    asic_evt_handlers[evtreg][evt] =
        (asic_evt_handler_entry_t) { handler, data };
    asic_evt_claim_level[evtreg][evt] = irqlevel;
    asic_evt_claim_active[evtreg][evt] = true;

    addr = ASIC_EVT_REG_ADDR(irqlevel, evtreg);
    OUT32(addr, IN32(addr) | BIT(evt));
    *claim = asic_evt_make_claim(code, generation);
    irq_restore(irq_state);
    return 0;
}

int asic_evt_claim_mask(asic_evt_claim_t claim) {
    irq_mask_t irq_state;
    uint8_t evtreg, evt, level;
    uint32_t addr;

    irq_state = irq_disable();

    if(!asic_evt_claim_valid(claim, &evtreg, &evt)) {
        irq_restore(irq_state);
        errno = ENOENT;
        return -1;
    }

    level = asic_evt_claim_level[evtreg][evt];
    addr = ASIC_EVT_REG_ADDR(level, evtreg);
    OUT32(addr, IN32(addr) & ~BIT(evt));
    irq_restore(irq_state);
    return 0;
}

int asic_evt_claim_unmask(asic_evt_claim_t claim) {
    irq_mask_t irq_state;
    uint8_t evtreg, evt, level;
    uint32_t addr;

    irq_state = irq_disable();

    if(!asic_evt_claim_valid(claim, &evtreg, &evt)) {
        irq_restore(irq_state);
        errno = ENOENT;
        return -1;
    }

    level = asic_evt_claim_level[evtreg][evt];
    addr = ASIC_EVT_REG_ADDR(level, evtreg);
    OUT32(addr, IN32(addr) | BIT(evt));
    irq_restore(irq_state);
    return 0;
}

int asic_evt_release(asic_evt_claim_t claim) {
    irq_mask_t irq_state;
    uint8_t evtreg, evt, level;
    uint32_t addr;

    irq_state = irq_disable();

    if(!asic_evt_claim_valid(claim, &evtreg, &evt)) {
        irq_restore(irq_state);
        errno = ENOENT;
        return -1;
    }

    for(level = 0; level < ASIC_IRQ_MAX; ++level) {
        addr = ASIC_EVT_REG_ADDR(level, evtreg);
        OUT32(addr, IN32(addr) & ~BIT(evt));
    }

    asic_evt_handlers[evtreg][evt] =
        (asic_evt_handler_entry_t) { NULL, NULL };
    asic_evt_claim_active[evtreg][evt] = false;
    asic_evt_claim_level[evtreg][evt] = ASIC_IRQ_DEFAULT;
    irq_restore(irq_state);
    return 0;
}

int asic_evt_get_status(uint16_t code, asic_evt_status_t *status) {
    irq_mask_t irq_state;
    uint8_t evtreg, evt, level, levels = 0;

    if(status)
        memset(status, 0, sizeof(*status));

    if(!asic_evt_code_valid(code) || !status) {
        errno = EINVAL;
        return -1;
    }

    evtreg = (uint8_t)(code >> 8);
    evt = (uint8_t)code;
    irq_state = irq_disable();

    for(level = 0; level < ASIC_IRQ_MAX; ++level) {
        if(IN32(ASIC_EVT_REG_ADDR(level, evtreg)) & BIT(evt))
            levels |= (uint8_t)BIT(level);
    }

    *status = (asic_evt_status_t) {
        .code = code,
        .enabled_levels = levels,
        .handler_present = asic_evt_handlers[evtreg][evt].hdl != NULL,
        .exclusively_claimed = asic_evt_claim_active[evtreg][evt],
        .dispatches = asic_evt_dispatches[evtreg][evt]
    };
    irq_restore(irq_state);
    return 0;
}

/* The ASIC event handler; this is called from the global IRQ handler
   to handle external IRQ 9. */
static void handler_irq9(irq_t source, irq_context_t *context, void *data) {
    const asic_evt_handler_entry_t (*const handlers)[ASIC_EVT_REG_HNDS] = data;
    const asic_evt_handler_entry_t *entry;
    uint8_t reg, i;

    (void)source;
    (void)context;

    /* Go through each event register and look for pending events */
    for(reg = 0; reg < ASIC_EVT_REGS; reg++) {
        /* Read the event mask and clear pending */
        uint32_t mask = IN32(ASIC_ACK_A + (reg * 0x4));
        OUT32(ASIC_ACK_A + (reg * 0x4), mask);

        /* Short circuit going through the table if none on this reg */
        if(mask == 0) continue;

        /* Search for relevant handlers */
        for(i = 0; i < ASIC_EVT_REG_HNDS; i++) {
            entry = &handlers[reg][i];

            if((mask & BIT(i)) && entry->hdl != NULL) {
                ++asic_evt_dispatches[reg][i];
                entry->hdl((reg << 8) | i, entry->data);
            }
        }
    }
}

/* Disable all G2 events */
void asic_evt_disable_all(void) {
    uint8_t irq, sub;

    for(irq = 0; irq < ASIC_IRQ_MAX; irq++) {
        for(sub = 0; sub < ASIC_EVT_REGS; sub++) {
            OUT32(ASIC_EVT_REG_ADDR(irq, sub), 0);
        }
    }
}

/* Disable a particular G2 event */
void asic_evt_disable(uint16_t code, uint8_t irqlevel) {
    uint8_t evtreg, evt;
    irq_mask_t irq_state;

    if(!asic_evt_code_valid(code) || irqlevel >= ASIC_IRQ_MAX) {
        errno = EINVAL;
        return;
    }

    evtreg = (code >> 8) & 0xff;
    evt = code & 0xff;
    irq_state = irq_disable();

    if(asic_evt_claim_active[evtreg][evt]) {
        irq_restore(irq_state);
        errno = EBUSY;
        return;
    }

    uint32_t addr = ASIC_EVT_REG_ADDR(irqlevel, evtreg);
    uint32_t val = IN32(addr);
    OUT32(addr, val & ~BIT(evt));
    irq_restore(irq_state);
}

/* Enable a particular G2 event */
void asic_evt_enable(uint16_t code, uint8_t irqlevel) {
    uint8_t evtreg, evt;
    irq_mask_t irq_state;

    if(!asic_evt_code_valid(code) || irqlevel >= ASIC_IRQ_MAX) {
        errno = EINVAL;
        return;
    }

    evtreg = (code >> 8) & 0xff;
    evt = code & 0xff;
    irq_state = irq_disable();

    if(asic_evt_claim_active[evtreg][evt]) {
        irq_restore(irq_state);
        errno = EBUSY;
        return;
    }

    uint32_t addr = ASIC_EVT_REG_ADDR(irqlevel, evtreg);
    uint32_t val = IN32(addr);
    OUT32(addr, val | BIT(evt));
    irq_restore(irq_state);
}

/* Initialize events */
static void asic_evt_init(void) {
    /* Clear any pending interrupts and disable all events */
    asic_evt_disable_all();
    OUT32(ASIC_ACK_A, 0xffffffff);
    OUT32(ASIC_ACK_B, 0xffffffff);
    OUT32(ASIC_ACK_C, 0xffffffff);

    /* Clear out the event table */
    memset(asic_evt_handlers, 0, sizeof(asic_evt_handlers));
    memset(asic_evt_claim_level, 0, sizeof(asic_evt_claim_level));
    memset(asic_evt_claim_active, 0, sizeof(asic_evt_claim_active));
    memset(asic_evt_dispatches, 0, sizeof(asic_evt_dispatches));
    memset(asic_evt_threaded, 0, sizeof(asic_evt_threaded));

    /* Hook IRQ9,B,D */
    irq_set_handler(EXC_IRQ9, handler_irq9, asic_evt_handlers);
    irq_set_handler(EXC_IRQB, handler_irq9, asic_evt_handlers);
    irq_set_handler(EXC_IRQD, handler_irq9, asic_evt_handlers);
}

/* Shutdown events */
static void asic_evt_shutdown(void) {
    uint8_t reg, evt;

    /* Disable all events */
    asic_evt_disable_all();

    /* Explicitly-created threaded handlers own workers that must be joined. */
    for(reg = 0; reg < ASIC_EVT_REGS; ++reg) {
        for(evt = 0; evt < ASIC_EVT_REG_HNDS; ++evt) {
            struct asic_thdata *thdata = asic_evt_threaded[reg][evt];

            asic_evt_handlers[reg][evt] =
                (asic_evt_handler_entry_t) { NULL, NULL };
            asic_evt_claim_active[reg][evt] = false;
            asic_evt_threaded[reg][evt] = NULL;

            if(thdata) {
                thd_worker_destroy(thdata->worker);
                free(thdata);
            }
        }
    }

    /* Unhook handlers */
    irq_set_handler(EXC_IRQ9, NULL, NULL);
    irq_set_handler(EXC_IRQB, NULL, NULL);
    irq_set_handler(EXC_IRQD, NULL, NULL);
}

/* Init routine */
void asic_init(void) {
    asic_evt_init();
}

void asic_shutdown(void) {
    asic_evt_shutdown();
}

static void asic_threaded_irq(void *data) {
    struct asic_thdata *thdata = data;

    thdata->hdl(thdata->source, thdata->data);

    if(thdata->unmask)
        thdata->unmask(thdata->source);
}

static void asic_thirq_dispatch(uint32_t source, void *data) {
    struct asic_thdata *thdata = data;

    if(thdata->ack_and_mask)
        thdata->ack_and_mask(source);

    thdata->source = source;

    thd_worker_wakeup(thdata->worker);
}

int asic_evt_request_threaded_handler(uint16_t code, asic_evt_handler hnd,
                                      void *data,
                                      void (*ack_and_mask)(uint16_t),
                                      void (*unmask)(uint16_t))
{
    struct asic_thdata *thdata;
    irq_mask_t irq_state;
    kthread_t *thd;
    uint8_t evtreg, evt;

    if(!asic_evt_code_valid(code) || !hnd) {
        errno = EINVAL;
        return -1;
    }

    thdata = malloc(sizeof(*thdata));
    if(!thdata)
        return -1;

    thdata->hdl = hnd;
    thdata->data = data;
    thdata->ack_and_mask = ack_and_mask;
    thdata->unmask = unmask;

    thdata->worker = thd_worker_create(asic_threaded_irq, thdata);
    if(!thdata->worker) {
        free(thdata);
        return -1;
    }

    /* Set a reasonable name to ID the thread */
    thd = thd_worker_get_thread(thdata->worker);
    snprintf(thd->label, KTHREAD_LABEL_SIZE,
             "Threaded IRQ code: 0x%x evt: 0x%.4x",
             ((code >> 16) & 0xf), (code & 0xffff));

    /* Highest priority */
    //thd_set_prio(thd, 0);

    evtreg = (uint8_t)(code >> 8);
    evt = (uint8_t)code;
    irq_state = irq_disable();

    if(asic_evt_handlers[evtreg][evt].hdl
            || asic_evt_claim_active[evtreg][evt]) {
        irq_restore(irq_state);
        thd_worker_destroy(thdata->worker);
        free(thdata);
        errno = EBUSY;
        return -1;
    }

    asic_evt_threaded[evtreg][evt] = thdata;
    asic_evt_handlers[evtreg][evt] =
        (asic_evt_handler_entry_t) { asic_thirq_dispatch, thdata };
    irq_restore(irq_state);

    return 0;
}

void asic_evt_remove_handler(uint16_t code)
{
    asic_evt_handler_entry_t entry;
    struct asic_thdata *thdata;
    uint8_t evtreg, evt;
    irq_mask_t irq_state;

    if(!asic_evt_code_valid(code)) {
        errno = EINVAL;
        return;
    }

    evtreg = (code >> 8) & 0xff;
    evt = code & 0xff;
    irq_state = irq_disable();

    if(asic_evt_claim_active[evtreg][evt]) {
        irq_restore(irq_state);
        errno = EBUSY;
        return;
    }

    entry = asic_evt_handlers[evtreg][evt];
    thdata = asic_evt_threaded[evtreg][evt];

    if(thdata && thd_get_current() == thd_worker_get_thread(thdata->worker)) {
        irq_restore(irq_state);
        errno = EDEADLK;
        return;
    }

    asic_evt_handlers[evtreg][evt] =
        (asic_evt_handler_entry_t) { NULL, NULL };
    asic_evt_threaded[evtreg][evt] = NULL;
    irq_restore(irq_state);

    if(thdata) {
        assert(thdata == entry.data);

        thd_worker_destroy(thdata->worker);
        free(thdata);
    }
}
