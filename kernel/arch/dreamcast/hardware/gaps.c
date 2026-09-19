/* KallistiOS ##version##

   dc/gaps.c

   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <dc/g2bus.h>
#include <dc/gaps.h>
#include <kos/irq.h>
#include "gaps_internal.h"

#define GAPS_BASE              0xa1000000u
#define GAPS_SIGNATURE         "GAPSPCI_BRIDGE_2"
#define GAPS_SIGNATURE_LENGTH  16u
#define GAPS_ALLOC_UNITS       (GAPS_SRAM_SIZE / GAPS_SRAM_ALIGNMENT)
#define GAPS_BITMAP_WORDS      (GAPS_ALLOC_UNITS / 32u)
#define GAPS_MAX_LEASES        32
#define GAPS_LEASE_INDEX_BITS  5u
#define GAPS_MAX_GENERATION    ((unsigned int)INT_MAX \
                                >> GAPS_LEASE_INDEX_BITS)

typedef struct gaps_lease_record {
    bool active;
    uint16_t first_unit;
    uint16_t unit_count;
    unsigned int generation;
    gaps_sram_dma_owner_t dma_owner;
} gaps_lease_record_t;

static uint32_t allocation_bitmap[GAPS_BITMAP_WORDS];
static gaps_lease_record_t leases[GAPS_MAX_LEASES];
static unsigned int next_generation = 1;
static unsigned int init_references;
static bool lifecycle_busy;

static bool unit_is_set(size_t unit) {
    return allocation_bitmap[unit >> 5] & (1u << (unit & 31u));
}

static void unit_set(size_t unit) {
    allocation_bitmap[unit >> 5] |= 1u << (unit & 31u);
}

static void unit_clear(size_t unit) {
    allocation_bitmap[unit >> 5] &= ~(1u << (unit & 31u));
}

static bool range_is_free(size_t first, size_t count) {
    size_t i;

    for(i = 0; i < count; ++i) {
        if(unit_is_set(first + i))
            return false;
    }

    return true;
}

static int publish_lease(size_t first, size_t count,
                         gaps_sram_lease_t *lease) {
    int i;
    size_t j;

    for(i = 0; i < GAPS_MAX_LEASES; ++i) {
        if(!leases[i].active)
            break;
    }

    if(i == GAPS_MAX_LEASES) {
        errno = ENOSPC;
        return -1;
    }

    for(j = 0; j < count; ++j)
        unit_set(first + j);

    leases[i].active = true;
    leases[i].first_unit = (uint16_t)first;
    leases[i].unit_count = (uint16_t)count;
    leases[i].generation = next_generation++;
    leases[i].dma_owner = 0;
    if(next_generation > GAPS_MAX_GENERATION)
        next_generation = 1;

    /* Five low bits select the record; the rest delay stale-handle reuse for
       more than sixty-seven million successful allocations. */
    *lease = (int)((leases[i].generation << GAPS_LEASE_INDEX_BITS)
                   | (unsigned int)i);
    return 0;
}

static gaps_lease_record_t *lookup_lease(gaps_sram_lease_t lease) {
    unsigned int index;
    unsigned int generation;

    if(lease < 0)
        return NULL;

    index = (unsigned int)lease & (GAPS_MAX_LEASES - 1u);
    generation = (unsigned int)lease >> GAPS_LEASE_INDEX_BITS;
    if(index >= GAPS_MAX_LEASES || !leases[index].active
            || leases[index].generation != generation)
        return NULL;

    return &leases[index];
}

int gaps_probe(void) {
    char signature[GAPS_SIGNATURE_LENGTH];

    g2_read_block_8((uint8_t *)signature, GAPS_BASE + 0x1400,
                    GAPS_SIGNATURE_LENGTH);
    return !memcmp(signature, GAPS_SIGNATURE, GAPS_SIGNATURE_LENGTH);
}

static int bridge_initialize(void) {
    int countdown;

    if(!gaps_probe()) {
        errno = ENODEV;
        return -1;
    }

    /* Quiesce the bridge before changing its aperture and PCI mapping. */
    g2_write_32(GAPS_BASE + 0x1414, 0x00000000);
    g2_write_32(GAPS_BASE + 0x1418, 0x5a14a501);

    countdown = 10000;
    while(!(g2_read_32(GAPS_BASE + 0x1418) & 1) && countdown > 0)
        --countdown;
    if(!(g2_read_32(GAPS_BASE + 0x1418) & 1)) {
        errno = ETIMEDOUT;
        return -1;
    }

    g2_write_32(GAPS_BASE + 0x1420, 0x01000000);
    g2_write_32(GAPS_BASE + 0x1424, 0x01000000);
    g2_write_32(GAPS_BASE + 0x1428, GAPS_SRAM_PHYS_BASE);
    g2_write_32(GAPS_BASE + 0x142c,
                GAPS_SRAM_PHYS_BASE + GAPS_SRAM_SIZE);
    g2_write_32(GAPS_BASE + 0x1414, 0x00000001);
    g2_write_32(GAPS_BASE + 0x1434, 0x00000001);

    /* Configure the fixed device behind the bridge. */
    g2_write_16(GAPS_BASE + 0x1606, 0xf900);
    g2_write_32(GAPS_BASE + 0x1630, 0x00000000);
    g2_write_8(GAPS_BASE + 0x163c, 0x00);
    g2_write_8(GAPS_BASE + 0x160d, 0xf0);
    g2_write_16(GAPS_BASE + 0x1604,
                g2_read_16(GAPS_BASE + 0x1604) | 0x6);
    g2_write_32(GAPS_BASE + 0x1614, 0x01000000);
    if(g2_read_8(GAPS_BASE + 0x1650) & 0x1) {
        g2_write_16(GAPS_BASE + 0x1654,
                    (g2_read_16(GAPS_BASE + 0x1654) & 0xfffc) | 0x8000);
    }
    g2_write_32(GAPS_BASE + 0x1414, 0x00000001);

    /* No client owns SRAM on first initialization, so clear the full window. */
    g2_memset_8(GAPS_SRAM_PHYS_BASE, 0, GAPS_SRAM_SIZE);

    /* Verify the bridge's writable handshake register before admitting users. */
    if(g2_read_32(GAPS_BASE + 0x141c) != 0x41474553)
        goto protocol_error;
    g2_write_32(GAPS_BASE + 0x141c, 0x55aaff00);
    if(g2_read_32(GAPS_BASE + 0x141c) != 0x55aaff00)
        goto protocol_error;
    g2_write_32(GAPS_BASE + 0x141c, 0xaa5500ff);
    if(g2_read_32(GAPS_BASE + 0x141c) != 0xaa5500ff)
        goto protocol_error;
    g2_write_32(GAPS_BASE + 0x141c, 0x41474553);
    return 0;

protocol_error:
    g2_write_32(GAPS_BASE + 0x1414, 0);
    errno = EPROTO;
    return -1;
}

int gaps_init(void) {
    irq_mask_t irq_state;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq_state = irq_disable();
    if(init_references) {
        if(init_references == UINT_MAX) {
            irq_restore(irq_state);
            errno = EOVERFLOW;
            return -1;
        }
        ++init_references;
        irq_restore(irq_state);
        return 0;
    }
    if(lifecycle_busy) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }
    lifecycle_busy = true;
    irq_restore(irq_state);

    if(bridge_initialize() < 0) {
        irq_state = irq_disable();
        lifecycle_busy = false;
        irq_restore(irq_state);
        return -1;
    }

    irq_state = irq_disable();
    memset(allocation_bitmap, 0, sizeof(allocation_bitmap));
    memset(leases, 0, sizeof(leases));
    init_references = 1;
    lifecycle_busy = false;
    irq_restore(irq_state);
    return 0;
}

int gaps_shutdown(void) {
    irq_mask_t irq_state;
    int i;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq_state = irq_disable();
    if(!init_references) {
        irq_restore(irq_state);
        errno = ENODEV;
        return -1;
    }
    if(init_references > 1) {
        --init_references;
        irq_restore(irq_state);
        return 0;
    }
    for(i = 0; i < GAPS_MAX_LEASES; ++i) {
        if(leases[i].active) {
            irq_restore(irq_state);
            errno = EBUSY;
            return -1;
        }
    }
    init_references = 0;
    g2_write_32(GAPS_BASE + 0x1414, 0);
    irq_restore(irq_state);
    return 0;
}

int gaps_sram_reserve(size_t offset, size_t size,
                      gaps_sram_lease_t *lease) {
    irq_mask_t irq_state;
    size_t first;
    size_t count;
    int rv;

    if(lease)
        *lease = GAPS_SRAM_LEASE_INVALID;
    if(!lease || !size || (offset & (GAPS_SRAM_ALIGNMENT - 1u))
            || (size & (GAPS_SRAM_ALIGNMENT - 1u))
            || offset >= GAPS_SRAM_SIZE || size > GAPS_SRAM_SIZE - offset) {
        errno = EINVAL;
        return -1;
    }

    first = offset / GAPS_SRAM_ALIGNMENT;
    count = size / GAPS_SRAM_ALIGNMENT;
    irq_state = irq_disable();
    if(!init_references) {
        irq_restore(irq_state);
        errno = ENODEV;
        return -1;
    }
    if(!range_is_free(first, count)) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }
    rv = publish_lease(first, count, lease);
    irq_restore(irq_state);
    return rv;
}

int gaps_sram_alloc(size_t size, size_t alignment,
                    gaps_sram_lease_t *lease) {
    irq_mask_t irq_state;
    size_t rounded_size;
    size_t count;
    size_t alignment_units;
    size_t first;
    int rv = -1;
    bool range_found = false;

    if(lease)
        *lease = GAPS_SRAM_LEASE_INVALID;
    if(!lease || !size || alignment < GAPS_SRAM_ALIGNMENT
            || alignment > GAPS_SRAM_SIZE
            || (alignment & (alignment - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(size > GAPS_SRAM_SIZE) {
        errno = ENOMEM;
        return -1;
    }

    rounded_size = (size + GAPS_SRAM_ALIGNMENT - 1u)
        & ~(GAPS_SRAM_ALIGNMENT - 1u);
    count = rounded_size / GAPS_SRAM_ALIGNMENT;
    alignment_units = alignment / GAPS_SRAM_ALIGNMENT;

    irq_state = irq_disable();
    if(!init_references) {
        irq_restore(irq_state);
        errno = ENODEV;
        return -1;
    }

    for(first = 0; first + count <= GAPS_ALLOC_UNITS;
            first += alignment_units) {
        if(range_is_free(first, count)) {
            range_found = true;
            rv = publish_lease(first, count, lease);
            break;
        }
    }
    irq_restore(irq_state);

    if(rv < 0 && !range_found)
        errno = ENOMEM;
    return rv;
}

int gaps_sram_get_info(gaps_sram_lease_t lease, gaps_sram_info_t *info) {
    irq_mask_t irq_state;
    gaps_lease_record_t *record;

    if(info)
        *info = (gaps_sram_info_t) { 0 };
    if(!info) {
        errno = EINVAL;
        return -1;
    }

    irq_state = irq_disable();
    record = lookup_lease(lease);
    if(!record) {
        irq_restore(irq_state);
        errno = EBADF;
        return -1;
    }
    info->offset = (size_t)record->first_unit * GAPS_SRAM_ALIGNMENT;
    info->size = (size_t)record->unit_count * GAPS_SRAM_ALIGNMENT;
    info->physical_address = GAPS_SRAM_PHYS_BASE + (uint32_t)info->offset;
    irq_restore(irq_state);
    return 0;
}

int gaps_sram_free(gaps_sram_lease_t lease) {
    irq_mask_t irq_state;
    gaps_lease_record_t *record;
    size_t i;

    irq_state = irq_disable();
    record = lookup_lease(lease);
    if(!record) {
        irq_restore(irq_state);
        errno = EBADF;
        return -1;
    }
    if(record->dma_owner) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }
    for(i = 0; i < record->unit_count; ++i)
        unit_clear(record->first_unit + i);
    record->active = false;
    irq_restore(irq_state);
    return 0;
}

int gaps_sram_dma_claim(gaps_sram_lease_t lease, size_t offset, size_t size,
                        gaps_sram_dma_owner_t owner,
                        uint32_t *physical_address) {
    irq_mask_t irq_state;
    gaps_lease_record_t *record;
    size_t lease_size;

    if(physical_address)
        *physical_address = 0;
    if(!physical_address || !size
            || (owner != GAPS_SRAM_DMA_OWNER_G1
                && owner != GAPS_SRAM_DMA_OWNER_G2)) {
        errno = EINVAL;
        return -1;
    }

    irq_state = irq_disable();
    record = lookup_lease(lease);
    if(!record) {
        irq_restore(irq_state);
        errno = EBADF;
        return -1;
    }
    lease_size = (size_t)record->unit_count * GAPS_SRAM_ALIGNMENT;
    if(offset >= lease_size || size > lease_size - offset
            || record->dma_owner) {
        irq_restore(irq_state);
        errno = offset >= lease_size || size > lease_size - offset
            ? EFAULT : EBUSY;
        return -1;
    }
    record->dma_owner = owner;
    *physical_address = GAPS_SRAM_PHYS_BASE
        + (uint32_t)record->first_unit * GAPS_SRAM_ALIGNMENT
        + (uint32_t)offset;
    irq_restore(irq_state);
    return 0;
}

int gaps_sram_dma_claim_address(uint32_t physical_address, size_t size,
                                gaps_sram_dma_owner_t owner,
                                gaps_sram_lease_t *lease) {
    irq_mask_t irq_state;
    size_t first;
    size_t last;
    int i;

    if(lease)
        *lease = GAPS_SRAM_LEASE_INVALID;
    if(!lease || !size || physical_address < GAPS_SRAM_PHYS_BASE
            || physical_address >= GAPS_SRAM_PHYS_BASE + GAPS_SRAM_SIZE
            || size > GAPS_SRAM_PHYS_BASE + GAPS_SRAM_SIZE
                        - physical_address
            || (owner != GAPS_SRAM_DMA_OWNER_G1
                && owner != GAPS_SRAM_DMA_OWNER_G2)) {
        errno = EINVAL;
        return -1;
    }

    first = (physical_address - GAPS_SRAM_PHYS_BASE)
        / GAPS_SRAM_ALIGNMENT;
    last = (physical_address - GAPS_SRAM_PHYS_BASE + size - 1u)
        / GAPS_SRAM_ALIGNMENT;
    irq_state = irq_disable();
    for(i = 0; i < GAPS_MAX_LEASES; ++i) {
        size_t lease_first;
        size_t lease_last;

        if(!leases[i].active)
            continue;
        lease_first = leases[i].first_unit;
        lease_last = lease_first + leases[i].unit_count - 1u;
        if(first >= lease_first && last <= lease_last) {
            if(leases[i].dma_owner) {
                irq_restore(irq_state);
                errno = EBUSY;
                return -1;
            }
            leases[i].dma_owner = owner;
            *lease = (int)((leases[i].generation
                            << GAPS_LEASE_INDEX_BITS) | (unsigned int)i);
            irq_restore(irq_state);
            return 0;
        }
    }
    irq_restore(irq_state);
    errno = EACCES;
    return -1;
}

void gaps_sram_dma_release(gaps_sram_lease_t lease,
                           gaps_sram_dma_owner_t owner) {
    irq_mask_t irq_state = irq_disable();
    gaps_lease_record_t *record = lookup_lease(lease);

    if(record && record->dma_owner == owner)
        record->dma_owner = 0;
    irq_restore(irq_state);
}
