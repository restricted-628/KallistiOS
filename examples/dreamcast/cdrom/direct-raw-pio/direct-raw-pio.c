/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/gdrom_direct.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "mmio-shim.h"
#include "../../../../kernel/arch/dreamcast/hardware/g1_bus.h"

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);

_Static_assert(GDROM_DIRECT_SECTOR_MODE1 == 0, "preserve cooked ABI");
_Static_assert(GDROM_DIRECT_SECTOR_MODE2_FORM1 == 1, "preserve cooked ABI");
static unsigned checks, failures, locks, unlocks;
static bool testing, held, mmio_error;
static enum { IDLE, PACKET, DATA, DONE } phase;
static uint8_t packet[12], control;
static unsigned packet_bytes;
static size_t response_bytes, consumed;
static uint16_t capacity;
static _Alignas(32) uint8_t storage[16 * GDROM_DIRECT_RAW_SECTOR_SIZE + 4];
static uint8_t *const buffer = storage + 2; /* Only two-byte aligned. */

#define CHECK(c) do { ++checks; if(!(c)) { ++failures; \
    printf("DIRECT-RAW-PIO: failed line=%d errno=%d\n", __LINE__, errno); \
} } while(0)

static uint16_t group_size(void) {
    size_t remaining = response_bytes - consumed;
    return remaining > 2048 ? 2048 : (uint16_t)remaining;
}

static uint8_t payload(size_t offset) { return (uint8_t)(offset * 17 + 3); }

uint8_t raw_probe_in8(uintptr_t address) {
    if(!testing || !held)
        mmio_error = true;
    if(address == G1_ATA_ALTSTATUS || address == G1_ATA_STATUS_REG)
        return G1_ATA_SR_DRDY | ((phase == PACKET || phase == DATA) ? G1_ATA_SR_DRQ : 0);
    if(address == G1_ATA_IRQ_REASON)
        return phase == PACKET ? G1_ATA_IR_COD : phase == DATA ? G1_ATA_IR_IO
            : G1_ATA_IR_COD | G1_ATA_IR_IO;
    if(address == G1_ATA_LBA_MID)
        return (uint8_t)group_size();
    if(address == G1_ATA_LBA_HIGH)
        return (uint8_t)(group_size() >> 8);
    mmio_error = true;
    return 0;
}

uint16_t raw_probe_in16(uintptr_t address) {
    uint16_t word;
    if(!held || address != G1_ATA_DATA || phase != DATA || consumed >= response_bytes)
        mmio_error = true;
    word = payload(consumed) | ((uint16_t)payload(consumed + 1) << 8);
    consumed += 2;
    if(consumed == response_bytes)
        phase = DONE;
    return word;
}

uint32_t raw_probe_in32(uintptr_t address) {
    (void)address;
    mmio_error = true; /* No Holly DMA register is read on this path. */
    return 0;
}

void raw_probe_out8(uintptr_t address, uint8_t value) {
    if(!testing || !held)
        mmio_error = true;
    if(address == G1_ATA_CTL)
        control = value;
    else if(address == G1_ATA_LBA_MID)
        capacity = (capacity & 0xff00) | value;
    else if(address == G1_ATA_LBA_HIGH)
        capacity = (capacity & 0xff) | ((uint16_t)value << 8);
    else if(address == G1_ATA_COMMAND_REG) {
        if(value != G1_ATA_CMD_PACKET || phase != IDLE)
            mmio_error = true;
        phase = PACKET;
    }
    else if(address != G1_ATA_FEATURES && address != G1_ATA_LBA_LOW)
        mmio_error = true;
}

void raw_probe_out16(uintptr_t address, uint16_t value) {
    if(!held || address != G1_ATA_DATA || phase != PACKET || packet_bytes >= 12) {
        mmio_error = true;
        return;
    }
    packet[packet_bytes++] = (uint8_t)value;
    packet[packet_bytes++] = (uint8_t)(value >> 8);
    if(packet_bytes == 12)
        phase = response_bytes ? DATA : DONE;
}

void raw_probe_out32(uintptr_t address, uint32_t value) {
    if(!held || (address != G1_ATA_PIO_RACCESS_WAIT && address != G1_ATA_PIO_WACCESS_WAIT)
            || value != G1_ACCESS_PIO_DEFAULT)
        mmio_error = true;
}

int __real_g1_bus_lock_timed(uint32_t timeout);
int __real_g1_bus_unlock(void);
int __wrap_g1_bus_lock_timed(uint32_t timeout) {
    if(!testing)
        return __real_g1_bus_lock_timed(timeout);
    CHECK(!held && timeout);
    ++locks;
    held = true;
    return 0;
}
int __wrap_g1_bus_unlock(void) {
    if(!testing)
        return __real_g1_bus_unlock();
    CHECK(held && phase == DONE && consumed == response_bytes && control == 0x08);
    ++unlocks;
    held = false;
    return 0;
}
int __wrap_g1_bus_select_device_timed(uint8_t device, uint32_t timeout, uint8_t *previous) {
    CHECK(held && device == 0 && timeout && !previous);
    return 0;
}
int __wrap_g1_bus_wait_status(uint8_t set, uint8_t clear, uint32_t timeout, uint8_t *status) {
    CHECK(held && !set && clear == (G1_ATA_SR_BSY | G1_ATA_SR_DRQ) && timeout);
    *status = G1_ATA_SR_DRDY;
    return 0;
}

static void run_read(gdrom_direct_sector_type_t type, size_t sectors, int extra, int error) {
    size_t expected = sectors * (type == GDROM_DIRECT_SECTOR_RAW2352 ? 2352 : 2048);
    gdrom_direct_result_t result;
    uint8_t flags = type == GDROM_DIRECT_SECTOR_RAW2352 ? 0x10
        : type == GDROM_DIRECT_SECTOR_MODE1 ? 0x24 : 0x28;
    uint8_t wanted[12] = { 0x30, flags, 0, 0, 150, 0, 0, 0, 0, 0, (uint8_t)sectors, 0 };
    bool bytes_ok = true;
    memset(storage, 0xa5, sizeof(storage));
    memset(&result, 0xa5, sizeof(result));
    response_bytes = (size_t)((int)expected + extra);
    consumed = 0;
    packet_bytes = locks = unlocks = 0;
    phase = IDLE;
    mmio_error = false;
    CHECK(gdrom_direct_read_sectors(buffer, 150, sectors, type, 10000, &result)
          == (error ? -1 : 0));
    CHECK(!error || errno == error);
    CHECK(locks == 1 && unlocks == 1 && !held && !mmio_error);
    CHECK(packet_bytes == 12 && !memcmp(packet, wanted, 12) && capacity == expected);
    CHECK(result.transferred == response_bytes && result.phase == GDROM_DIRECT_PHASE_COMPLETE
          && !result.sense_valid && !result.recovery_attempted);
    for(size_t i = 0; i < expected; ++i)
        if(buffer[i] != (i < response_bytes ? payload(i) : 0xa5))
            bytes_ok = false;
    CHECK(bytes_ok && storage[0] == 0xa5 && storage[1] == 0xa5
          && buffer[expected] == 0xa5 && buffer[expected + 1] == 0xa5);
}

int main(void) {
    gdrom_direct_result_t result;
    testing = true;
    run_read(GDROM_DIRECT_SECTOR_MODE1, 1, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_MODE2_FORM1, 16, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 1, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 3, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 16, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 1, -2, EPROTO);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 1, 2, EMSGSIZE);
    locks = 0;
#define REJECT(b, f, n, t, ms) do { \
    memset(&result, 0xa5, sizeof(result)); \
    CHECK(gdrom_direct_read_sectors(b, f, n, t, ms, &result) == -1 && errno == EINVAL); \
    CHECK(!locks && result.transferred == 0 && !result.sense_valid); \
} while(0)
    REJECT(NULL, 150, 1, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(buffer + 1, 150, 1, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(buffer, 149, 1, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(buffer, 150, 0, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(buffer, 150, 17, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(buffer, 0xffffff, 2, GDROM_DIRECT_SECTOR_RAW2352, 1000);
    REJECT(buffer, 150, 1, (gdrom_direct_sector_type_t)99, 1000);
    REJECT(buffer, 150, 1, GDROM_DIRECT_SECTOR_RAW2352, 0);
    CHECK(gdrom_direct_read_sectors_dma(storage, 150, 2, GDROM_DIRECT_SECTOR_RAW2352,
                                       1000, &result) == -1 && errno == EINVAL && !locks);
    CHECK(!gdrom_direct_read_sectors_dma_async(storage, 150, 2, GDROM_DIRECT_SECTOR_RAW2352,
                                              1000, NULL, NULL, NULL) && errno == EINVAL);
    CHECK(!gdrom_direct_sector_range_open(150, 2, GDROM_DIRECT_SECTOR_RAW2352)
          && errno == EINVAL);
    CHECK(!gdrom_direct_stream_session_start(150, 2, GDROM_DIRECT_SECTOR_RAW2352, 1000, 1000)
          && errno == EINVAL);
    testing = false;
    printf("DIRECT-RAW-PIO: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
