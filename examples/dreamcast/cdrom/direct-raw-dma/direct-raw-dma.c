/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/asic.h>
#include <dc/gdrom_direct.h>
#include <dc/gaps.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "mmio-shim.h"
#include "../../../../kernel/arch/dreamcast/hardware/g1_bus.h"
#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"
#include "../../../../kernel/arch/dreamcast/hardware/gaps_internal.h"

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);

static unsigned checks, failures, locks, unlocks, invalidations, submissions;
static unsigned claims, releases, packet_bytes, starts;
static bool testing, held, packet_phase, done, masked, cancelled;
static uint8_t packet[12], control, feature, mode;
static uint32_t address, length, protection, direction, enabled, transferred;
static uintptr_t wanted_address;
static size_t wanted_bytes, progress_bytes, lease_size;
static int transfer_delta;
static bool reverse_events, fail_claim;
static g1_bus_dma_irq_handler_t dma_handler, command_handler;
static void *dma_data, *command_data;
static semaphore_t event;
static cdrom_request_executor_t execute;
static _Alignas(8) uint8_t captured[128];
static cdrom_request_t *const request = (cdrom_request_t *)0x1234;
static uint64_t fake_time;
static uint32_t unlock_step, lock_step, budgets[8], fads[8], counts[8];
static uintptr_t destinations[8];
static size_t total_sectors, wire_size;
static unsigned short_command, fail_lock;
static bool chaining;

uint64_t dma_probe_time(void) { return fake_time; }

#define CHECK(c) do { ++checks; if(!(c)) { ++failures; \
    printf("DIRECT-RAW-DMA: failed line=%d errno=%d\n", __LINE__, errno); \
} } while(0)

uint8_t dma_probe_in8(uintptr_t reg) {
    CHECK(held);
    if(reg == G1_ATA_ALTSTATUS || reg == G1_ATA_STATUS_REG)
        return G1_ATA_SR_DRDY | (packet_phase ? G1_ATA_SR_DRQ : 0);
    if(reg == G1_ATA_IRQ_REASON)
        return packet_phase ? G1_ATA_IR_COD : G1_ATA_IR_COD | G1_ATA_IR_IO;
    CHECK(false);
    return 0;
}
uint16_t dma_probe_in16(uintptr_t reg) {
    (void)reg;
    CHECK(false); /* No PIO payload fallback. */
    return 0;
}
uint32_t dma_probe_in32(uintptr_t reg) {
    CHECK(held);
    if(reg == G1_ATA_DMA_CURRENT_LEN)
        return transferred;
    if(reg == G1_ATA_DMA_CURRENT_ADDR)
        return address + transferred;
    CHECK(false);
    return 0;
}
void dma_probe_out8(uintptr_t reg, uint8_t value) {
    CHECK(held);
    if(reg == G1_ATA_CTL)
        control = value;
    else if(reg == G1_ATA_FEATURES)
        feature = value;
    else if(reg == G1_ATA_SECTOR_COUNT)
        mode = value;
    else if(reg == G1_ATA_COMMAND_REG) {
        if(value == G1_ATA_CMD_SET_FEATURES)
            CHECK(feature == G1_ATA_FEATURE_XFER_MODE && mode == G1_ATA_XFER_WDMA(2));
        else {
            CHECK(value == G1_ATA_CMD_PACKET && feature == G1_ATA_FEATURE_DMA);
            CHECK(enabled == 1 && length == wanted_bytes);
            packet_phase = true;
        }
    }
    else
        CHECK((reg == G1_ATA_LBA_LOW || reg == G1_ATA_LBA_MID
                || reg == G1_ATA_LBA_HIGH) && !value);
}
void dma_probe_out16(uintptr_t reg, uint16_t value) {
    CHECK(held && reg == G1_ATA_DATA && packet_phase && packet_bytes < 12);
    if(packet_bytes >= 12)
        return;
    packet[packet_bytes++] = (uint8_t)value;
    packet[packet_bytes++] = (uint8_t)(value >> 8);
    if(packet_bytes == 12)
        packet_phase = false;
    if(packet_bytes == 12 && chaining) {
        CHECK(locks > 0 && locks <= 8);
        if(locks <= 8) {
            fads[locks - 1] = (packet[2] << 16) | (packet[3] << 8) | packet[4];
            counts[locks - 1] = (packet[8] << 16) | (packet[9] << 8) | packet[10];
        }
    }
}
void dma_probe_out32(uintptr_t reg, uint32_t value) {
    CHECK(held);
    switch(reg) {
        case ASIC_ACK_B: CHECK(value == 1); break;
        case G1_ATA_PIO_RACCESS_WAIT:
        case G1_ATA_PIO_WACCESS_WAIT: CHECK(value == G1_ACCESS_PIO_DEFAULT); break;
        case G1_ATA_DMA_RACCESS_WAIT: CHECK(value == G1_ACCESS_WDMA_MODE2); break;
        case G1_ATA_DMA_PROTECTION_P2: protection = value; break;
        case G1_ATA_DMA_ADDRESS: address = value; break;
        case G1_ATA_DMA_LENGTH: length = value; break;
        case G1_ATA_DMA_DIRECTION: direction = value; break;
        case G1_ATA_DMA_ENABLE: enabled = value; break;
        case G1_ATA_DMA_STATUS:
            CHECK(value == 1 && enabled == 1 && direction == G1_DMA_TO_MEMORY);
            CHECK(packet_bytes == 12 && dma_handler && command_handler);
            CHECK(address == (wanted_address & 0x1fffffffu) && length == wanted_bytes);
            CHECK(protection == (0x88430000u | (((address >> 20) & 127u) << 8)
                                 | (((address + length - 1) >> 20) & 127u)));
            ++starts;
            transferred = (uint32_t)((int)length +
                (short_command ? (starts == short_command ? -32 : 0) : transfer_delta));
            done = true;
            if(reverse_events) {
                CHECK(command_handler(ASIC_EVT_GD_COMMAND, command_data));
                CHECK(dma_handler(ASIC_EVT_GD_DMA, dma_data));
            }
            else {
                CHECK(dma_handler(ASIC_EVT_GD_DMA, dma_data));
                CHECK(command_handler(ASIC_EVT_GD_COMMAND, command_data));
            }
            break;
        default: CHECK(false); break;
    }
}
void dma_probe_invalidate(uintptr_t addr, size_t size) {
    CHECK(held && addr == wanted_address && size == wanted_bytes);
    ++invalidations;
}

int __real_g1_bus_lock_timed(uint32_t timeout);
int __real_g1_bus_unlock(void);
void __real_thd_schedule(bool front);
int __wrap_g1_bus_lock_timed(uint32_t timeout) {
    if(!testing) return __real_g1_bus_lock_timed(timeout);
    CHECK(!held && timeout);
    if(chaining) {
        size_t completed = locks * 16;
        size_t chunk = total_sectors - completed;
        if(chunk > 16) chunk = 16;
        CHECK(locks < 8 && completed < total_sectors);
        if(locks < 8) budgets[locks] = timeout;
        if(locks) wanted_address += wanted_bytes;
        wanted_bytes = chunk * wire_size;
        if(locks < 8) destinations[locks] = wanted_address;
        packet_bytes = 0;
        packet_phase = done = false;
        transferred = 0;
    }
    if(fail_lock && locks + 1 == fail_lock) {
        ++locks;
        errno = EBUSY;
        return -1;
    }
    fake_time += lock_step;
    held = true;
    ++locks;
    return 0;
}
int __wrap_g1_bus_unlock(void) {
    if(!testing) return __real_g1_bus_unlock();
    CHECK(held && !dma_handler && !command_handler && !masked);
    CHECK(cancelled || (done && control == 0x08 && !enabled));
    held = false;
    ++unlocks;
    fake_time += unlock_step;
    return 0;
}
void __wrap_thd_schedule(bool front) {
    if(!testing) __real_thd_schedule(front);
}
int __wrap_g1_bus_select_device_timed(uint8_t device, uint32_t timeout, uint8_t *previous) {
    CHECK(held && !device && timeout && !previous);
    return 0;
}
int __wrap_g1_bus_wait_status(uint8_t set, uint8_t clear, uint32_t timeout, uint8_t *status) {
    CHECK(held && !set && clear == (G1_ATA_SR_BSY | G1_ATA_SR_DRQ) && timeout);
    *status = G1_ATA_SR_DRDY;
    return 0;
}
g1_bus_dma_client_t __wrap_g1_bus_dma_client_register(g1_bus_dma_irq_handler_t fn, void *data) {
    CHECK(held && !dma_handler);
    dma_handler = fn; dma_data = data;
    return 1;
}
int __wrap_g1_bus_dma_client_unregister(g1_bus_dma_client_t client) {
    CHECK(held && client == 1 && dma_handler);
    dma_handler = NULL;
    return 0;
}
int __wrap_g1_bus_gd_command_client_register(g1_bus_dma_irq_handler_t fn, void *data) {
    CHECK(held && !command_handler);
    command_handler = fn; command_data = data;
    return 0;
}
int __wrap_g1_bus_gd_command_client_unregister(void) {
    CHECK(held && command_handler);
    command_handler = NULL;
    return 0;
}
int __wrap_g1_bus_gd_command_client_mask(void) { masked = true; return 0; }
int __wrap_g1_bus_gd_command_client_unmask(void) { masked = false; return 0; }
int __wrap_g1_bus_dma_in_progress(void) { return 0; }
void __wrap_g1_bus_dma_disable(void) { CHECK(held); enabled = 0; }

cdrom_request_t *__wrap_cdrom_request_submit_executor(
        cd_cmd_code_t command, const void *params, size_t params_size,
        size_t requested, size_t data, size_t io, uint32_t timeout,
        cdrom_request_executor_t executor, cdrom_request_finalizer_t finalizer,
        void *finalizer_data, cdrom_request_callback_t callback, void *callback_data) {
    CHECK(command == CD_CMD_DMAREAD && params_size <= sizeof(captured) && timeout);
    CHECK(requested == wanted_bytes && data == wanted_bytes && io == wanted_bytes);
    CHECK(!finalizer && !finalizer_data && !callback && !callback_data && !locks);
    if(params_size <= sizeof(captured)) memcpy(captured, params, params_size);
    execute = executor;
    ++submissions;
    return request;
}
semaphore_t *__wrap_cdrom_request_event_internal(cdrom_request_t *r) {
    CHECK(r == request); return &event;
}
bool __wrap_cdrom_request_cancel_requested_internal(const cdrom_request_t *r) {
    CHECK(r == request); return cancelled;
}
void __wrap_cdrom_request_update_direct_progress(cdrom_request_t *r, size_t bytes) {
    CHECK(r == request); progress_bytes = bytes;
}
int __wrap_gaps_sram_get_info(gaps_sram_lease_t lease, gaps_sram_info_t *info) {
    CHECK(lease == 7);
    *info = (gaps_sram_info_t) { .physical_address = GAPS_SRAM_PHYS_BASE,
                               .size = lease_size };
    return 0;
}
int __wrap_gaps_sram_dma_claim(gaps_sram_lease_t lease, size_t offset, size_t size,
        gaps_sram_dma_owner_t owner, uint32_t *physical) {
    CHECK(lease == 7 && offset == 32 && size == wanted_bytes && owner == GAPS_SRAM_DMA_OWNER_G1);
    ++claims;
    if(fail_claim) { errno = EBUSY; return -1; }
    *physical = GAPS_SRAM_PHYS_BASE + 32;
    return 0;
}
void __wrap_gaps_sram_dma_release(gaps_sram_lease_t lease, gaps_sram_dma_owner_t owner) {
    CHECK(lease == 7 && owner == GAPS_SRAM_DMA_OWNER_G1 && !held);
    ++releases;
}

static void prepare(uintptr_t dest, size_t bytes) {
    CHECK(!held && !dma_handler && !command_handler);
    wanted_address = dest; wanted_bytes = bytes;
    locks = unlocks = starts = invalidations = submissions = claims = releases = 0;
    packet_bytes = 0; memset(packet, 0, sizeof(packet));
    address = length = protection = direction = enabled = transferred = 0;
    packet_phase = done = masked = cancelled = fail_claim = reverse_events = false;
    transfer_delta = 0; progress_bytes = 0; execute = NULL;
    lease_size = GAPS_SRAM_SIZE;
    chaining = false;
    fake_time = 1000;
    unlock_step = lock_step = short_command = fail_lock = 0;
    memset(budgets, 0, sizeof(budgets));
    memset(fads, 0, sizeof(fads));
    memset(counts, 0, sizeof(counts));
    memset(destinations, 0, sizeof(destinations));
}
static void verify(gdrom_direct_sector_type_t type, unsigned sectors, unsigned cache_calls) {
    uint8_t flags = type == GDROM_DIRECT_SECTOR_RAW2352 ? 0x10
        : type == GDROM_DIRECT_SECTOR_MODE1 ? 0x24 : 0x28;
    uint8_t expected[12] = { 0x30, flags, 0, 0, 150, 0, 0, 0, 0, 0, sectors, 0 };
    CHECK(!memcmp(packet, expected, 12) && length == wanted_bytes);
    CHECK(locks == 1 && unlocks == 1 && starts == 1 && !held);
    CHECK(invalidations == cache_calls);
}
static void chain_case(uintptr_t dest, size_t sectors,
                       gdrom_direct_sector_type_t type, unsigned fail_at,
                       uint32_t step, uint32_t timeout, unsigned lock_failure) {
    gdrom_direct_result_t result;
    size_t size = type == GDROM_DIRECT_SECTOR_RAW2352 ? 2352 : 2048;
    unsigned commands = (sectors + 15) / 16;
    unsigned expected_starts = commands;
    int expected_error = 0;

    prepare(dest, 16 * size);
    chaining = true;
    total_sectors = sectors;
    wire_size = size;
    short_command = fail_at;
    fail_lock = lock_failure;
    unlock_step = step;
    lock_step = 3;
    if(fail_at) { expected_starts = fail_at; expected_error = EPROTO; }
    if(lock_failure) { expected_starts = lock_failure - 1; expected_error = EBUSY; }
    if(step && (commands - 1) * (step + lock_step) >= timeout) {
        expected_starts = (timeout + step + lock_step - 1) / (step + lock_step);
        expected_error = ETIMEDOUT;
    }
    int rv = gdrom_direct_read_sectors_dma((void *)dest, 150, sectors, type,
                                          timeout, &result);
    CHECK(rv == (expected_error ? -1 : 0));
    CHECK(!expected_error || errno == expected_error);
    CHECK(starts == expected_starts && unlocks == starts && !held);
    CHECK(locks == starts + (lock_failure ? 1 : 0));
    CHECK(!submissions && !claims);
    size_t expected_bytes = expected_error ? expected_starts * 16 * size : sectors * size;
    if(fail_at) {
        size_t completed_sectors = fail_at * 16;
        if(completed_sectors > sectors) completed_sectors = sectors;
        expected_bytes = completed_sectors * size - 32;
    }
    CHECK(result.transferred == expected_bytes);
    CHECK(invalidations == ((dest & 0xe0000000u) == 0x80000000u
          && (dest & 0x1c000000u) == 0x0c000000u ? starts * 2 : 0));
    if(!expected_error || fail_at)
        CHECK(result.dma_transferred == wanted_bytes - (fail_at ? 32 : 0));
    else
        CHECK(result.dma_transferred == 0 && result.phase == 0);
    for(unsigned i = 0; i < starts; ++i) {
        size_t chunk = sectors - i * 16;
        if(chunk > 16) chunk = 16;
        CHECK(fads[i] == 150 + i * 16 && counts[i] == chunk);
        CHECK(destinations[i] == dest + i * 16 * size);
        CHECK(budgets[i] == timeout - i * (step + lock_step));
    }
}

int main(void) {
    gdrom_direct_result_t result;
    testing = true;
    sem_init(&event, 0);
    for(unsigned n = 2; n <= 16; n += 2) {
        prepare(0x8c200000, n * 2352);
        reverse_events = (n & 2) != 0;
        CHECK(!gdrom_direct_read_sectors_dma((void *)wanted_address, 150, n,
              GDROM_DIRECT_SECTOR_RAW2352, 1000, &result));
        CHECK(result.transferred == wanted_bytes && result.dma_transferred == wanted_bytes
              && result.phase == GDROM_DIRECT_PHASE_COMPLETE);
        verify(GDROM_DIRECT_SECTOR_RAW2352, n, 2);
    }
    for(unsigned t = 0; t < 2; ++t) {
        prepare(0x8c200000, 2048);
        CHECK(!gdrom_direct_read_sectors_dma((void *)wanted_address, 150, 1, t, 1000, &result));
        verify(t, 1, 2);
    }
    prepare(0xac200000, 4704);
    transfer_delta = -32;
    CHECK(gdrom_direct_read_sectors_dma((void *)wanted_address, 150, 2,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result) == -1 && errno == EPROTO);
    CHECK(result.transferred == 4672);
    verify(GDROM_DIRECT_SECTOR_RAW2352, 2, 0);
    for(unsigned n = 1; n <= 17; n += 2) {
        prepare(0x8c200000, n * 2352);
        CHECK(gdrom_direct_read_sectors_dma((void *)wanted_address, 150, n,
              GDROM_DIRECT_SECTOR_RAW2352, 1000, &result) == -1 && errno == EINVAL);
        CHECK(!gdrom_direct_read_sectors_dma_async((void *)wanted_address, 150, n,
              GDROM_DIRECT_SECTOR_RAW2352, 1000, &result, NULL, NULL) && errno == EINVAL);
        CHECK(gdrom_direct_read_sectors_dma_gaps(7, 32, 150, n,
              GDROM_DIRECT_SECTOR_RAW2352, 1000, &result) == -1 && errno == EINVAL);
        CHECK(!gdrom_direct_read_sectors_dma_gaps_async(7, 32, 150, n,
              GDROM_DIRECT_SECTOR_RAW2352, 1000, &result, NULL, NULL) && errno == EINVAL);
        CHECK(!locks && !submissions && !claims);
    }
    prepare(0x8c200000, 4704);
#define REJECT(dest, fad, count, type, timeout) do { \
    CHECK(gdrom_direct_read_sectors_dma((void *)(dest), fad, count, type, timeout, \
          &result) == -1 && errno == EINVAL); \
    CHECK(!gdrom_direct_read_sectors_dma_async((void *)(dest), fad, count, type, \
          timeout, &result, NULL, NULL) && errno == EINVAL); \
    CHECK(!locks && !submissions); \
} while(0)
    REJECT(0, 150, 2, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(0x8c200010, 150, 2, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(0x8c200000, 149, 2, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(0x8c200000, 0xffffff, 2, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(0x8c200000, 150, 0, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    CHECK(gdrom_direct_read_sectors_dma_gaps(7, 32, 150, 18,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result) == -1 && errno == EINVAL);
    REJECT(0x8c200000, 150, 2, (gdrom_direct_sector_type_t)99, 1000);
    REJECT(0x8c200000, 150, 2, GDROM_DIRECT_SECTOR_RAW2352, 0);
    /* 4096 bytes remain: two cooked sectors fit, two raw sectors do not. */
    CHECK(gdrom_direct_read_sectors_dma((void *)(0x8c000000u + HW_MEMSIZE - 4096),
          150, 2, GDROM_DIRECT_SECTOR_RAW2352, 1000, &result) == -1 && errno == EFAULT);
    CHECK(!locks && result.transferred == 0);
    /* Deferred execution uses the copied format and byte counts. */
    prepare(0x8c200000, 4704);
    CHECK(gdrom_direct_read_sectors_dma_async((void *)wanted_address, 150, 2,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result, NULL, NULL) == request);
    CHECK(submissions == 1 && execute && execute(request, captured) == ERR_OK);
    CHECK(progress_bytes == 4704 && result.transferred == 4704);
    verify(GDROM_DIRECT_SECTOR_RAW2352, 2, 2);
    prepare(0x8c200000, 4704);
    CHECK(gdrom_direct_read_sectors_dma_async((void *)wanted_address, 150, 2,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result, NULL, NULL) == request);
    cancelled = true;
    CHECK(execute(request, captured) == ERR_ABORTED && !starts && locks == unlocks);
    for(unsigned async = 0; async < 2; ++async) {
        prepare(0xa0000000u + GAPS_SRAM_PHYS_BASE + 32, 4704);
        if(async) {
            CHECK(gdrom_direct_read_sectors_dma_gaps_async(7, 32, 150, 2,
                  GDROM_DIRECT_SECTOR_RAW2352, 1000, &result, NULL, NULL) == request);
            CHECK(!claims && execute(request, captured) == ERR_OK);
        }
        else CHECK(!gdrom_direct_read_sectors_dma_gaps(7, 32, 150, 2,
                   GDROM_DIRECT_SECTOR_RAW2352, 1000, &result));
        CHECK(claims == 1 && releases == 1 && result.transferred == 4704);
        verify(GDROM_DIRECT_SECTOR_RAW2352, 2, 0);
    }
    prepare(0xa0000000u + GAPS_SRAM_PHYS_BASE + 32, 4704);
    lease_size = 4096 + 32; /* Cooked accounting would incorrectly admit this. */
    CHECK(!gdrom_direct_read_sectors_dma_gaps_async(7, 32, 150, 2,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result, NULL, NULL) && errno == EFAULT);
    CHECK(!submissions && !claims);
    prepare(0xa0000000u + GAPS_SRAM_PHYS_BASE + 32, 4704);
    CHECK(gdrom_direct_read_sectors_dma_gaps_async(7, 32, 150, 2,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result, NULL, NULL) == request);
    fail_claim = true;
    CHECK(execute(request, captured) == ERR_BUSY && !locks && !releases);

    chain_case(0x8c200000, 17, GDROM_DIRECT_SECTOR_MODE1, 0, 10, 1000, 0);
    chain_case(0x8c200000, 33, GDROM_DIRECT_SECTOR_MODE2_FORM1, 0, 10, 1000, 0);
    chain_case(0xac200000, 18, GDROM_DIRECT_SECTOR_RAW2352, 0, 10, 1000, 0);
    chain_case(0x8c200000, 32, GDROM_DIRECT_SECTOR_RAW2352, 0, 10, 1000, 0);
    chain_case(0xa4000000, 34, GDROM_DIRECT_SECTOR_RAW2352, 0, 10, 1000, 0);
    chain_case(0x8c200000, 34, GDROM_DIRECT_SECTOR_RAW2352, 2, 10, 1000, 0);
    chain_case(0x8c200000, 34, GDROM_DIRECT_SECTOR_RAW2352, 3, 10, 1000, 0);
    chain_case(0x8c200000, 33, GDROM_DIRECT_SECTOR_MODE1, 0, 60, 100, 0);
    chain_case(0x8c200000, 33, GDROM_DIRECT_SECTOR_MODE1, 0, 10, 1000, 2);
    prepare(0x8c200000, 0);
    /* First segment fits, but the complete range does not. No partial I/O. */
    CHECK(gdrom_direct_read_sectors_dma((void *)(0x8c000000u + HW_MEMSIZE - 32768),
          150, 17, GDROM_DIRECT_SECTOR_MODE1, 1000, &result) == -1 && errno == EFAULT);
    CHECK(gdrom_direct_read_sectors_dma((void *)0xa47f8000u, 150, 18,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result) == -1 && errno == EFAULT);
    CHECK(gdrom_direct_read_sectors_dma((void *)0xffff8000u, 150, 18,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result) == -1 && errno == EFAULT);
    CHECK(gdrom_direct_read_sectors_dma((void *)0x8c200000u, 0xfffff0, 18,
          GDROM_DIRECT_SECTOR_RAW2352, 1000, &result) == -1 && errno == EINVAL);
    CHECK(gdrom_direct_read_sectors_dma((void *)0x8c200000u, 150, SIZE_MAX,
          GDROM_DIRECT_SECTOR_MODE1, 1000, &result) == -1 && errno == EINVAL);
    CHECK(!locks && !starts && !invalidations && result.transferred == 0);
    sem_destroy(&event);
    testing = false;
    printf("DIRECT-RAW-DMA: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
