/* KallistiOS ##version##

   snd_mem.c
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2023, 2025 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <dc/sound/sound.h>
#include <arch/arch.h>
#include <kos/dbglog.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <dc/spu.h>
#include "arm/aica_cmd_iface.h"
#include "snd_mem_reservation.h"

/*

This is a very simple allocator for SPU RAM. I decided not to go with dlmalloc
because of the massive number of changes it would require in the thing to
make it use the g2_* bus calls. This is just a lot more sane.

This still uses the same basic algorithm, though, it just doesn't bother
trying to be super efficient. This is based on the assumption that the
most common usage will be allocating or freeing a couple of very large
chunks every once in a while, not a ton of tiny chunks constantly (like
dlmalloc is optimized for).

The malloc algorithm used here is a basic "best fit" algorithm. We store a
linked list of available chunks of sound RAM in regular RAM and traverse it
to find the chunk that is the smallest while still large enough to fit the
block size we want. If there is any space left over, this chunk is broken
into two chunks, the first one occupied and the second one unoccupied.

The free algorithm is very lazy: it attempts to coalesce with neighbor
blocks if any of them are free. Otherwise it simply tags the block as free
in the hopes that a later free will coalesce with it.

This system is a bit more simplistic than the standard "buckets" system, but
I figure we don't need that sort of complexity in this code.

*/

#define SNDMEMDEBUG 0

/* A single block of SPU RAM */
typedef struct snd_block_str {
    /* Our queue entry */
    TAILQ_ENTRY(snd_block_str)  qent;

    /* The address of this block (offset from SPU RAM base) */
    uint32_t  addr;

    /* The size of this block */
    size_t  size;

    /* Is this block in use? */
    bool inuse;
    wave_reservation_t *reservation;
} snd_block_t;

/* Our SPU RAM pool */
static bool initted = false;
static TAILQ_HEAD(snd_block_q, snd_block_str) pool = {0};
static mutex_t snd_mem_mutex = MUTEX_INITIALIZER;
static uint32_t pool_base;
static uint32_t pool_size;
static size_t live_reservations;
static uint64_t last_reservation;
static kthread_t *reconfiguration_owner;

static int snd_mem_reservation_lock(void) {
    irq_mask_t saved = irq_disable();
    irq_restore(saved);
    if(irq_inside_int() || (saved & 0x100000f0u)) {
        errno = EPERM;
        return -1;
    }
    if(mutex_lock_irqsafe(&snd_mem_mutex)) {
        errno = EAGAIN;
        return -1;
    }

    return 0;
}

static int snd_mem_reservation_unlock(int error) {
    mutex_unlock(&snd_mem_mutex);
    if(error) {
        errno = error;
        return -1;
    }
    return 0;
}

/* Serialize with reservations without holding the allocator lock across SPU
   and driver operations. Only the successful transition owner may end it. */
int snd_mem_reconfigure_begin(void) {
    if(snd_mem_reservation_lock() < 0)
        return -1;

    if(live_reservations || reconfiguration_owner)
        return snd_mem_reservation_unlock(EBUSY);
    reconfiguration_owner = thd_get_current();
    return snd_mem_reservation_unlock(reconfiguration_owner ? 0 : ENODEV);
}

int snd_mem_reconfigure_end(void) {
    if(snd_mem_reservation_lock() < 0)
        return -1;

    if(!reconfiguration_owner || reconfiguration_owner != thd_get_current())
        return snd_mem_reservation_unlock(EPERM);
    reconfiguration_owner = NULL;
    return snd_mem_reservation_unlock(0);
}

int snd_mem_reconfigure_require_owner(void) {
    if(snd_mem_reservation_lock() < 0)
        return -1;
    return snd_mem_reservation_unlock(reconfiguration_owner &&
        reconfiguration_owner == thd_get_current() ? 0 : EPERM);
}

static void snd_mem_clear_locked(void) {
    snd_block_t *block, *next;
    assert(!live_reservations);

    TAILQ_FOREACH_SAFE(block, &pool, qent, next) {
        TAILQ_REMOVE(&pool, block, qent);
        free(block);
    }

    initted = false;
    pool_base = 0;
    pool_size = 0;
}

/* Reinitialize the pool with the given RAM base offset. */
static int snd_mem_init_internal(uint32_t reserve, bool in_transition) {
    snd_block_t *blk;
    uint32_t ram_size;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    if(mutex_lock_irqsafe(&snd_mem_mutex)) {
        errno = EAGAIN;
        return -1;
    }

    if(live_reservations || (in_transition
            ? !reconfiguration_owner || reconfiguration_owner != thd_get_current()
            : reconfiguration_owner != NULL))
        return snd_mem_reservation_unlock(EBUSY);

    ram_size = hardware_sys_mode(NULL) == HW_TYPE_RETAIL ?
               2 * 1024 * 1024 : 8 * 1024 * 1024;

    if(reserve > ram_size || reserve > UINT32_MAX - 31) {
        mutex_unlock(&snd_mem_mutex);
        errno = EINVAL;
        return -1;
    }

    /* Sound-RAM allocations are aligned for DMA and store-queue copies. */
    reserve = __align_up(reserve, 32);

    if(reserve >= ram_size) {
        mutex_unlock(&snd_mem_mutex);
        errno = ENOSPC;
        return -1;
    }

    blk = (snd_block_t *)malloc(sizeof(snd_block_t));

    if(!blk) {
        mutex_unlock(&snd_mem_mutex);
        errno = ENOMEM;
        return -1;
    }

    /* Allocate the replacement descriptor before discarding the existing
       pool, so a main-memory failure leaves the old allocator usable. */
    if(initted)
        snd_mem_clear_locked();

    TAILQ_INIT(&pool);

    memset(blk, 0, sizeof(snd_block_t));
    blk->addr = reserve;

    blk->size = ram_size - reserve;

    blk->inuse = false;
    TAILQ_INSERT_HEAD(&pool, blk, qent);

    dbglog(DBG_SOURCE(SNDMEMDEBUG), "snd_mem_init: %zu bytes available\n", blk->size);

    initted = true;
    pool_base = reserve;
    pool_size = (uint32_t)blk->size;
    mutex_unlock(&snd_mem_mutex);

    return 0;
}

int snd_mem_init(uint32_t reserve) {
    return snd_mem_init_internal(reserve, false);
}

int snd_mem_reconfigure_init(uint32_t reserve) {
    return snd_mem_init_internal(reserve, true);
}

static int snd_mem_shutdown_internal(bool in_transition) {
    if(mutex_lock_irqsafe(&snd_mem_mutex)) {
        errno = EAGAIN;
        return -1;
    }

    if(live_reservations || (in_transition
            ? !reconfiguration_owner || reconfiguration_owner != thd_get_current()
            : reconfiguration_owner != NULL))
        return snd_mem_reservation_unlock(EBUSY);
    if(initted)
        snd_mem_clear_locked();
    return snd_mem_reservation_unlock(0);
}

void snd_mem_shutdown(void) {
    (void)snd_mem_shutdown_internal(false);
}

int snd_mem_reconfigure_shutdown(void) {
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    return snd_mem_shutdown_internal(true);
}

static snd_block_t *snd_mem_alloc_locked(size_t size) {
    snd_block_t *e, *best = NULL;
    size_t best_size = SIZE_MAX;

    if(!initted) {
        errno = ENODEV;
        return NULL;
    }

    if(size > SIZE_MAX - 31 || size > pool_size) {
        errno = ENOMEM;
        return NULL;
    }

    /* Make sure the size is a multiple of 32 bytes to maintain alignment. */
    size = __align_up(size, 32);

    /* Look for a block */
    TAILQ_FOREACH(e, &pool, qent) {
        if(e->size >= size && e->size < best_size && !e->inuse) {
            best_size = e->size;
            best = e;
        }
    }

    if(best == NULL) {
        dbglog(DBG_ERROR, "snd_mem_malloc: no chunks big enough for alloc(%zu)\n", size);
        errno = ENOMEM;
        return NULL;
    }

    /* Is the block the exact size? */
    if(best->size == size) {
        dbglog(DBG_SOURCE(SNDMEMDEBUG), "snd_mem_malloc: allocating perfect-fit at %08lx for size %zu\n",
               best->addr, best->size);

        best->inuse = true;
        return best;
    }

    /* Nope: break it up into two chunks */
    e = (snd_block_t*)malloc(sizeof(snd_block_t));

    if(e == NULL) {
        dbglog(DBG_ERROR, "snd_mem_malloc: not enough main memory to alloc(%zu)\n", size);
        errno = ENOMEM;
        return NULL;
    }

    memset(e, 0, sizeof(snd_block_t));
    e->addr = best->addr + size;
    e->size = best->size - size;
    e->inuse = false;
    TAILQ_INSERT_AFTER(&pool, best, e, qent);

    dbglog(DBG_SOURCE(SNDMEMDEBUG), "snd_mem_malloc: allocating block %08lx for size %zu, and leaving %zu at %08lx\n",
               best->addr, size, e->size, e->addr);

    best->size = size;
    best->inuse = true;

    return best;
}

/* Allocate a chunk of SPU RAM and return its wave RAM offset. */
uint32_t snd_mem_malloc(size_t size) {
    if(!size) {
        errno = EINVAL;
        return 0;
    }

    if(mutex_lock_irqsafe(&snd_mem_mutex)) {
        errno = EAGAIN;
        return 0;
    }

    snd_block_t *block = snd_mem_alloc_locked(size);
    uint32_t offset = block ? block->addr : 0;
    mutex_unlock(&snd_mem_mutex);
    return offset;
}

static void snd_mem_free_locked(snd_block_t *e) {
    snd_block_t *o;

    /* Set this block as unused */
    e->inuse = false;

    dbglog(DBG_SOURCE(SNDMEMDEBUG), "snd_mem_free: freeing block at %08lx\n", e->addr);

    /* Can we coalesce with the block before us? */
    o = TAILQ_PREV(e, snd_block_q, qent);

    if(o && !o->inuse) {
        dbglog(DBG_SOURCE(SNDMEMDEBUG), "   coalescing with block at %08lx\n", o->addr);

        o->size += e->size;
        TAILQ_REMOVE(&pool, e, qent);
        free(e);
        e = o;
    }

    /* Can we coalesce with the block in front of us? */
    o = TAILQ_NEXT(e, qent);

    if(o && !o->inuse) {
        dbglog(DBG_SOURCE(SNDMEMDEBUG), "   coalescing with block at %08lx\n", o->addr);

        e->size += o->size;
        TAILQ_REMOVE(&pool, o, qent);
        free(o);
    }
}

/* Free a chunk of SPU RAM; pointer is expected to be an offset into
   SPU RAM. */
void snd_mem_free(uint32_t addr) {
    snd_block_t *e;

    if(addr == 0)
        return;

    if(mutex_lock_irqsafe(&snd_mem_mutex))
        return;

    if(!initted) {
        mutex_unlock(&snd_mem_mutex);
        errno = ENODEV;
        return;
    }

    /* Look for the block */
    TAILQ_FOREACH(e, &pool, qent) {
        if(e->addr == addr)
            break;
    }

    if(!e) {
        dbglog(DBG_ERROR, "snd_mem_free: attempt to free non-existent block at %08lx\n",
               addr);
        mutex_unlock(&snd_mem_mutex);
        errno = EINVAL;
        return;
    }

    if(!e->inuse) {
        dbglog(DBG_ERROR, "snd_mem_free: duplicate free at %08lx\n", addr);
        mutex_unlock(&snd_mem_mutex);
        errno = EALREADY;
        return;
    }

    if(e->reservation) {
        mutex_unlock(&snd_mem_mutex);
        errno = EBUSY;
        return;
    }

    snd_mem_free_locked(e);
    mutex_unlock(&snd_mem_mutex);
}

uint32_t snd_mem_available(void) {
    snd_block_t *e;
    size_t largest = 0;

    if(!initted)
        return 0;

    if(mutex_lock_irqsafe(&snd_mem_mutex)) {
        errno = EAGAIN;
        return 0;
    }

    TAILQ_FOREACH(e, &pool, qent) {
        if(!e->inuse && e->size > largest)
            largest = e->size;
    }

    mutex_unlock(&snd_mem_mutex);
    return (uint32_t)largest;
}

int snd_mem_get_status(snd_mem_status_t *status) {
    snd_block_t *block;

    if(!status) {
        errno = EINVAL;
        return -1;
    }

    if(mutex_lock_irqsafe(&snd_mem_mutex)) {
        errno = EAGAIN;
        return -1;
    }

    memset(status, 0, sizeof(*status));
    status->initialized = initted;
    status->pool_base = pool_base;
    status->pool_size = pool_size;

    TAILQ_FOREACH(block, &pool, qent) {
        if(block->inuse) {
            status->allocated_bytes += (uint32_t)block->size;
            ++status->allocated_blocks;
        }
        else {
            status->free_bytes += (uint32_t)block->size;
            ++status->free_blocks;

            if(block->size > status->largest_free)
                status->largest_free = (uint32_t)block->size;
        }
    }

    mutex_unlock(&snd_mem_mutex);
    return 0;
}

/* The allocator owns the allocation, writer and token in one transaction.
   No caller receives a wave RAM offset until it adopts a complete block. */
static snd_block_t *snd_mem_find_reservation_locked(uint64_t owner) {
    snd_block_t *block;

    TAILQ_FOREACH(block, &pool, qent) {
        if(block->reservation && block->reservation->owner == owner)
            return block;
    }
    return NULL;
}

int snd_mem_reserve_wave(size_t bytes, uint64_t *owner) {
    if(!owner || !bytes || (bytes & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(snd_mem_reservation_lock() < 0)
        return -1;

    if(reconfiguration_owner)
        return snd_mem_reservation_unlock(EBUSY);
    if(hardware_sys_mode(NULL) != HW_TYPE_RETAIL)
        return snd_mem_reservation_unlock(ENOTSUP);
    if(!initted || pool_base < AICA_RAM_START || pool_base >= SPU_RAM_SIZE_2MB
            || pool_size > SPU_RAM_SIZE_2MB - pool_base)
        return snd_mem_reservation_unlock(ENODEV);
    if(last_reservation == UINT64_MAX)
        return snd_mem_reservation_unlock(EOVERFLOW);

    wave_reservation_t *state = calloc(1, sizeof(*state));
    if(!state)
        return snd_mem_reservation_unlock(ENOMEM);
    snd_block_t *block = snd_mem_alloc_locked(bytes);
    if(!block) {
        int error = errno;
        free(state);
        return snd_mem_reservation_unlock(error);
    }

    state->owner = last_reservation;
    int error = wave_reserve(state, block->addr, block->size, &last_reservation);
    if(error) {
        snd_mem_free_locked(block);
        free(state);
    }
    else {
        block->reservation = state;
        ++live_reservations;
        *owner = last_reservation;
    }
    return snd_mem_reservation_unlock(error);
}

int snd_mem_wave_begin(uint64_t owner, size_t offset, size_t length,
                       wave_write_t *writer, uint32_t *destination) {
    if(snd_mem_reservation_lock() < 0)
        return -1;

    snd_block_t *block = snd_mem_find_reservation_locked(owner);
    int error = block ? wave_begin(block->reservation, owner, offset, length,
                                   writer, destination) : ESTALE;
    return snd_mem_reservation_unlock(error);
}

int snd_mem_wave_finish(wave_write_t writer, int error, size_t transferred) {
    if(snd_mem_reservation_lock() < 0)
        return -1;

    snd_block_t *block = snd_mem_find_reservation_locked(writer.owner);
    return snd_mem_reservation_unlock(block
        ? wave_finish(block->reservation, writer, error, transferred)
        : ESTALE);
}

int snd_mem_wave_close(uint64_t owner, uint32_t *published) {
    if(snd_mem_reservation_lock() < 0)
        return -1;

    snd_block_t *block = snd_mem_find_reservation_locked(owner);
    uint32_t offset;
    int error = block ? wave_close(block->reservation, owner, published != NULL, &offset)
                      : ESTALE;
    if(!error) {
        free(block->reservation);
        block->reservation = NULL;
        --live_reservations;
        if(published)
            *published = offset;
        else
            snd_mem_free_locked(block);
    }
    return snd_mem_reservation_unlock(error);
}
