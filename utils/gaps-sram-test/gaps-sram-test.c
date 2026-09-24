/* KallistiOS ##version##

   gaps-sram-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/gaps.h>
#include <dc/fs_dcload.h>
#include <kos/irq.h>
#include "gaps_internal.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAPS_BASE UINT32_C(0xa1000000)

static uint8_t register_space[0x1800];
static uint8_t sram[GAPS_SRAM_SIZE];
static uint32_t irq_depth;
static bool inside_irq;
static unsigned int failures;
static unsigned int checks;
static unsigned int writes;
static bool loader_present;
static gaps_owner_t owner;
static bool reenter;
int dcload_type = DCLOAD_TYPE_NONE;

int syscall_dcload_detected(void) { return loader_present; }

#define CHECK(condition) do { \
    ++checks; \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

static uint8_t *map_address(uintptr_t address, size_t size) {
    if(address >= GAPS_BASE
            && address - GAPS_BASE <= sizeof(register_space) - size)
        return register_space + (address - GAPS_BASE);
    if(address >= GAPS_SRAM_PHYS_BASE
            && address - GAPS_SRAM_PHYS_BASE <= sizeof(sram) - size)
        return sram + (address - GAPS_SRAM_PHYS_BASE);
    fprintf(stderr, "invalid simulated G2 address %08lx\n",
            (unsigned long)address);
    abort();
}

irq_mask_t irq_disable(void) {
    irq_mask_t previous = irq_depth;
    ++irq_depth;
    return previous;
}

void irq_restore(irq_mask_t state) {
    irq_depth = state;
}

bool irq_inside_int(void) {
    return inside_irq;
}

uint8_t g2_read_8(uintptr_t address) {
    return *map_address(address, 1);
}

void g2_write_8(uintptr_t address, uint8_t value) {
    ++writes;
    *map_address(address, 1) = value;
}

uint16_t g2_read_16(uintptr_t address) {
    uint16_t value;
    memcpy(&value, map_address(address, sizeof(value)), sizeof(value));
    return value;
}

void g2_write_16(uintptr_t address, uint16_t value) {
    ++writes;
    memcpy(map_address(address, sizeof(value)), &value, sizeof(value));
}

uint32_t g2_read_32(uintptr_t address) {
    uint32_t value;
    memcpy(&value, map_address(address, sizeof(value)), sizeof(value));
    return value;
}

void g2_write_32(uintptr_t address, uint32_t value) {
    ++writes;
    if(reenter) {
        gaps_owner_t nested;
        reenter = false;
        CHECK(gaps_get_role() == GAPS_ROLE_TRANSITION);
        CHECK(gaps_acquire(GAPS_ROLE_NETWORK, &nested) < 0 && errno == EBUSY);
        CHECK(nested == GAPS_OWNER_INVALID);
    }
    memcpy(map_address(address, sizeof(value)), &value, sizeof(value));
}

void g2_read_block_8(uint8_t *output, uintptr_t address, size_t amount) {
    memcpy(output, map_address(address, amount), amount);
}

void g2_memset_8(uintptr_t address, uint8_t value, size_t amount) {
    ++writes;
    CHECK(g2_read_8(GAPS_BASE + 0x1737) == 0);
    CHECK(g2_read_16(GAPS_BASE + 0x173c) == 0);
    memset(map_address(address, amount), value, amount);
}

static void prepare_bridge(void) {
    uint32_t handshake = UINT32_C(0x41474553);

    memset(register_space, 0, sizeof(register_space));
    memset(sram, 0xa5, sizeof(sram));
    memcpy(register_space + 0x1400, "GAPSPCI_BRIDGE_2", 16);
    memcpy(register_space + 0x141c, &handshake, sizeof(handshake));
}

static void test_allocator_and_stale_handles(void) {
    gaps_sram_lease_t whole;
    gaps_sram_lease_t extra;
    gaps_sram_info_t info;
    size_t i;

    CHECK(gaps_sram_alloc(owner, GAPS_SRAM_SIZE, GAPS_SRAM_ALIGNMENT,
                          &whole) == 0);
    CHECK(gaps_sram_get_info(whole, &info) == 0);
    CHECK(info.offset == 0 && info.size == GAPS_SRAM_SIZE);
    CHECK(info.physical_address == GAPS_SRAM_PHYS_BASE);
    errno = 0;
    CHECK(gaps_sram_alloc(owner, 32, 32, &extra) < 0 && errno == ENOMEM);
    CHECK(gaps_release(owner) < 0 && errno == EBUSY);
    CHECK(gaps_get_role() == GAPS_ROLE_STAGING);
    CHECK(gaps_sram_free(whole) == 0);
    errno = 0;
    CHECK(gaps_sram_get_info(whole, &info) < 0 && errno == EBADF);

    for(i = 0; i < sizeof(sram); ++i)
        CHECK(sram[i] == 0);
}

static void test_fixed_layout_and_dma_exclusion(void) {
    gaps_sram_lease_t rx;
    gaps_sram_lease_t guard;
    gaps_sram_lease_t tx;
    gaps_sram_lease_t overlap;
    gaps_sram_lease_t claimed;
    uint32_t address;

    CHECK(gaps_sram_reserve(owner, 0, 0x4000, &rx) == 0);
    CHECK(gaps_sram_reserve(owner, 0x4000, 0x2000, &guard) == 0);
    CHECK(gaps_sram_reserve(owner, 0x6000, 0x2000, &tx) == 0);
    errno = 0;
    CHECK(gaps_sram_reserve(owner, 0x2000, 0x2000, &overlap) < 0
          && errno == EBUSY);

    CHECK(gaps_sram_dma_claim(tx, 0, 2048, GAPS_SRAM_DMA_OWNER_G1,
                              &address) == 0);
    CHECK(address == GAPS_SRAM_PHYS_BASE + 0x6000);
    errno = 0;
    CHECK(gaps_sram_dma_claim_address(address, 2048,
                                      GAPS_SRAM_DMA_OWNER_G2,
                                      &claimed) < 0 && errno == EBUSY);
    errno = 0;
    CHECK(gaps_sram_free(tx) < 0 && errno == EBUSY);
    CHECK(gaps_sram_dma_claim(rx, 0, 32, GAPS_SRAM_DMA_OWNER_G2,
                              &address) < 0 && errno == EBUSY);
    CHECK(gaps_sram_dma_claim_address(GAPS_SRAM_PHYS_BASE, 32,
          GAPS_SRAM_DMA_OWNER_G2, &claimed) < 0 && errno == EBUSY);
    /* A stale or wrong-owner release must not unlock the window. */
    gaps_sram_dma_release(tx, GAPS_SRAM_DMA_OWNER_G2);
    CHECK(gaps_sram_free(tx) < 0 && errno == EBUSY);
    gaps_sram_dma_release(tx, GAPS_SRAM_DMA_OWNER_G1);

    CHECK(gaps_sram_dma_claim_address(GAPS_SRAM_PHYS_BASE + 0x6000, 2048,
                                      GAPS_SRAM_DMA_OWNER_G2,
                                      &claimed) == 0);
    CHECK(claimed == tx);
    gaps_sram_dma_release(claimed, GAPS_SRAM_DMA_OWNER_G2);
    CHECK(gaps_sram_free(tx) == 0);
    CHECK(gaps_sram_free(guard) == 0);
    CHECK(gaps_sram_free(rx) == 0);
}

static void test_owner_and_loader(void) {
    gaps_owner_t other = 99, stale = owner;
    gaps_sram_lease_t lease, claimed;
    uint32_t address;
    unsigned int before = writes;

    CHECK(gaps_acquire(GAPS_ROLE_NETWORK, &other) < 0 && errno == EBUSY);
    CHECK(other == GAPS_OWNER_INVALID && writes == before);
    CHECK(gaps_release(owner + 1) < 0 && errno == EBADF);
    CHECK(gaps_sram_alloc(owner + 1, 32, 32, &lease) < 0 && errno == EBADF);
    CHECK(gaps_release(owner) == 0);
    CHECK(gaps_release(owner) < 0 && errno == EBADF);
    CHECK(gaps_get_role() == GAPS_ROLE_NONE);

    loader_present = true;
    dcload_type = DCLOAD_TYPE_IP;
    before = writes;
    CHECK(gaps_get_role() == GAPS_ROLE_LOADER);
    CHECK(gaps_native_loader_allowed());
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, &other) < 0 && errno == EBUSY);
    CHECK(other == GAPS_OWNER_INVALID && writes == before);
    /* Even INIT_NO_DCLOAD/unknown loader must not be mistaken for free SRAM. */
    dcload_type = DCLOAD_TYPE_NONE;
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, &other) < 0 && errno == EBUSY);
    CHECK(writes == before);
    dcload_type = DCLOAD_TYPE_IP;

    CHECK(gaps_acquire(GAPS_ROLE_NETWORK, &owner) == 0);
    CHECK(owner != stale && gaps_get_role() == GAPS_ROLE_NETWORK);
    CHECK(!gaps_native_loader_allowed());
    CHECK(g2_read_16(GAPS_BASE + 0x1604) & 4);
    CHECK(gaps_sram_alloc(owner, 32, 32, &lease) == 0);
    /* Networking ownership does not disable G1. The owner may authorize a
       transfer with its lease; it must separately coordinate NIC activity. */
    CHECK(gaps_sram_dma_claim(lease, 0, 32, GAPS_SRAM_DMA_OWNER_G1,
                              &address) == 0);
    CHECK(address == GAPS_SRAM_PHYS_BASE);
    CHECK(gaps_sram_dma_claim_address(address, 32,
          GAPS_SRAM_DMA_OWNER_G2, &claimed) < 0 && errno == EBUSY);
    CHECK(claimed == GAPS_SRAM_LEASE_INVALID);
    CHECK(gaps_sram_free(lease) < 0 && errno == EBUSY);
    gaps_sram_dma_release(lease, GAPS_SRAM_DMA_OWNER_G1);
    CHECK(gaps_sram_dma_claim_address(address, 32,
          GAPS_SRAM_DMA_OWNER_G1, &claimed) == 0);
    CHECK(claimed == lease);
    gaps_sram_dma_release(claimed, GAPS_SRAM_DMA_OWNER_G1);
    CHECK(gaps_release(owner) < 0 && errno == EBUSY);
    CHECK(gaps_sram_dma_claim(lease, 0, 32, GAPS_SRAM_DMA_OWNER_G2, &address) == 0);
    CHECK(gaps_sram_free(lease) < 0 && errno == EBUSY);
    gaps_sram_dma_release(lease, GAPS_SRAM_DMA_OWNER_G2);
    CHECK(gaps_sram_free(lease) == 0);
    CHECK(gaps_release(owner) == 0);
    CHECK(gaps_get_role() == GAPS_ROLE_LOADER);
    CHECK(gaps_native_loader_allowed());
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, &other) < 0 && errno == EBUSY);

    dcload_type = DCLOAD_TYPE_SER;
    CHECK(gaps_get_role() == GAPS_ROLE_NONE);
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, &owner) == 0);
    CHECK(gaps_native_loader_allowed()); /* Serial never uses this bridge. */
    CHECK(gaps_sram_alloc(stale, 32, 32, &lease) < 0 && errno == EBADF);
    inside_irq = true;
    CHECK(gaps_release(owner) < 0 && errno == EPERM);
    CHECK(gaps_sram_alloc(owner, 32, 32, &lease) < 0 && errno == EPERM);
    CHECK(gaps_sram_reserve(owner, 0, 32, &lease) < 0 && errno == EPERM);
    CHECK(gaps_acquire(GAPS_ROLE_NETWORK, &other) < 0 && errno == EPERM);
    inside_irq = false;
    CHECK(gaps_release(owner) == 0);
    loader_present = false;
    dcload_type = DCLOAD_TYPE_NONE;

    /* Failed initialization must not publish a token or strand the transition. */
    register_space[0x1400] = 0;
    before = writes;
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, &other) < 0 && errno == ENODEV);
    CHECK(other == GAPS_OWNER_INVALID && writes == before);
    CHECK(gaps_get_role() == GAPS_ROLE_NONE);
    prepare_bridge();
    register_space[0x141c] = 0;
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, &other) < 0 && errno == EPROTO);
    CHECK(other == GAPS_OWNER_INVALID && gaps_get_role() == GAPS_ROLE_NONE);
    prepare_bridge();
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, &other) == 0);
    CHECK(gaps_release(other) == 0);
    before = writes;
    CHECK(gaps_acquire(GAPS_ROLE_LOADER, &other) < 0 && errno == EINVAL);
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, NULL) < 0 && errno == EINVAL);
    CHECK(writes == before);
}

int main(void) {
    prepare_bridge();
    CHECK(gaps_probe() == 1);
    reenter = true;
    CHECK(gaps_acquire(GAPS_ROLE_STAGING, &owner) == 0);
    CHECK(gaps_get_role() == GAPS_ROLE_STAGING);
    CHECK(!(g2_read_16(GAPS_BASE + 0x1604) & 4));

    test_allocator_and_stale_handles();
    test_fixed_layout_and_dma_exclusion();

    test_owner_and_loader();
    CHECK(irq_depth == 0);

    if(failures) {
        fprintf(stderr, "%u GAPS SRAM checks failed\n", failures);
        return EXIT_FAILURE;
    }

    printf("GAPS SRAM tests passed: %u checks\n", checks);
    return EXIT_SUCCESS;
}
