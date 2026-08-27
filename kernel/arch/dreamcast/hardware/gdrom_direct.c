/* KallistiOS ##version##

   hardware/gdrom_direct.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <arch/arch.h>
#include <arch/mmu.h>
#include <dc/asic.h>
#include <dc/gaps.h>
#include <dc/gdrom_direct.h>
#include <dc/memory.h>

#include <kos/cache.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>

#include "g1_bus.h"
#include "cdrom_request.h"
#include "gdrom_direct_internal.h"
#include "gdrom_spi.h"
#include "gaps_internal.h"

/* Register ordering in this file is a load-bearing part of the transport.

   ATA packet setup writes task-file arguments before COMMAND. Direct Holly
   DMA writes protection/timing, destination, length, direction, then enables
   the engine; SB_GDST is asserted only after the drive has accepted the SPI
   packet. Reordering either sequence can expose an unprotected destination,
   lose the packet-phase interrupt, or start Holly before the drive is ready.

   SET FEATURES 0xEF with feature 0x03 and sector-count value 0x22 selects
   WDMA mode 2. That drive-side setting is separate from Holly's 0x1001 access
   timing and both must be restored after a reset. */
#define G1_IN8(address)   (*(volatile uint8_t *)(address))
#define G1_IN16(address)  (*(volatile uint16_t *)(address))
#define G1_IN32(address)  (*(volatile uint32_t *)(address))
#define G1_OUT8(address, value) \
    (*(volatile uint8_t *)(address) = (uint8_t)(value))
#define G1_OUT16(address, value) \
    (*(volatile uint16_t *)(address) = (uint16_t)(value))
#define G1_OUT32(address, value) \
    (*(volatile uint32_t *)(address) = (uint32_t)(value))

/* Device-control bit 3 is fixed at one in SPI; bit 1 disables INTRQ. */
#define GDROM_CTL_INTERRUPTS_ON  0x08u
#define GDROM_CTL_INTERRUPTS_OFF 0x0au

#define GDROM_REASON_MASK        (G1_ATA_IR_IO | G1_ATA_IR_COD)
#define GDROM_REASON_DATA_OUT    0u
#define GDROM_REASON_PACKET_OUT  G1_ATA_IR_COD
#define GDROM_REASON_DATA_IN     G1_ATA_IR_IO
#define GDROM_REASON_STATUS      (G1_ATA_IR_IO | G1_ATA_IR_COD)

#define GDROM_DMA_PROTECTION_KEY 0x88430000u
#define GDROM_DMA_CLEANUP_MS     100u
#define GDROM_COMMAND_RECOVERY_MS 1000u
#define GDROM_DMA_PROGRESS_POLL_MS 16u

typedef struct gdrom_direct_dma_operation {
    semaphore_t *event;
    volatile bool active;
    volatile bool command_event;
    volatile bool command_masked;
    volatile bool dma_event;
    volatile uint32_t dma_code;
} gdrom_direct_dma_operation_t;

typedef bool (*gdrom_direct_cancel_t)(void *data);
typedef void (*gdrom_direct_progress_t)(size_t bytes, void *data);

typedef struct gdrom_direct_async_read {
    void *buffer;
    gaps_sram_lease_t gaps_lease;
    size_t gaps_offset;
    uint32_t fad;
    size_t sectors;
    gdrom_direct_sector_type_t sector_type;
    uint32_t timeout;
    gdrom_direct_result_t *result;
} gdrom_direct_async_read_t;

typedef enum gdrom_dma_destination {
    GDROM_DMA_DESTINATION_INVALID = 0,
    GDROM_DMA_DESTINATION_SYSTEM_RAM,
    GDROM_DMA_DESTINATION_VRAM,
    GDROM_DMA_DESTINATION_GAPS_SRAM
} gdrom_dma_destination_t;

typedef struct gdrom_direct_async_seek {
    uint32_t fad;
    uint32_t timeout;
    gdrom_direct_result_t *result;
} gdrom_direct_async_seek_t;

typedef enum gdrom_direct_mode_operation {
    GDROM_DIRECT_MODE_GET = 0,
    GDROM_DIRECT_MODE_SET
} gdrom_direct_mode_operation_t;

typedef struct gdrom_direct_async_mode {
    gdrom_direct_mode_operation_t operation;
    gdrom_direct_mode_settings_t settings;
    gdrom_direct_mode_t *mode;
    uint32_t timeout;
    gdrom_direct_result_t *result;
} gdrom_direct_async_mode_t;

typedef struct gdrom_direct_async_reinit {
    gdrom_direct_reinit_result_t *result;
    uint32_t timeout;
} gdrom_direct_async_reinit_t;

typedef enum gdrom_direct_cdda_operation {
    GDROM_DIRECT_CDDA_PLAY = 0,
    GDROM_DIRECT_CDDA_PAUSE,
    GDROM_DIRECT_CDDA_RESUME,
    GDROM_DIRECT_CDDA_STOP,
    GDROM_DIRECT_CDDA_SCAN,
    GDROM_DIRECT_CDDA_STATUS
} gdrom_direct_cdda_operation_t;

typedef struct gdrom_direct_async_cdda {
    gdrom_direct_cdda_operation_t operation;
    uint32_t start;
    uint32_t end;
    uint32_t loops;
    int mode;
    bool reverse;
    uint8_t speed;
    uint32_t timeout;
    gdrom_direct_result_t *result;
    cdrom_cdda_status_t *status;
} gdrom_direct_async_cdda_t;

struct gdrom_direct_stream {
    gdrom_direct_dma_operation_t operation;
    g1_bus_dma_client_t dma_client;
    bool command_client;
    bool command_active;
    bool bus_faulted;
    size_t total_bytes;
    size_t transferred_bytes;
};

typedef enum gdrom_direct_dma_test_mode {
    GDROM_DIRECT_DMA_TEST_NONE = 0,
    GDROM_DIRECT_DMA_TEST_ABORT_AFTER_START,
    GDROM_DIRECT_DMA_TEST_EXCLUDED_PROTECTION
} gdrom_direct_dma_test_mode_t;

_Static_assert(sizeof(cd_toc_t) == GDROM_SPI_TOC_SIZE,
               "KOS and SPI TOC sizes must remain identical");

static int wait_next_phase(uint64_t deadline, uint8_t *status_out,
                           gdrom_direct_result_t *result,
                           gdrom_direct_cancel_t cancel, void *cancel_data);
static int set_dma_mode2(uint64_t deadline,
                         gdrom_direct_result_t *result);
static int capture_check_sense_locked(
    gdrom_direct_result_t *result,
    gdrom_direct_result_t *error_transport);
static int deadline_timeout(uint64_t deadline, uint32_t *timeout);

static bool direct_command_irq(uint32_t code, void *data) {
    gdrom_direct_dma_operation_t *operation = data;

    if(code != ASIC_EVT_GD_COMMAND || !operation->active)
        return false;

    /* INTRQ is level-backed, but task-file access while SB_GDST is active is
       prohibited and can hang older Holly revisions. Mask it here; the owner
       acknowledges Status and rearms the event only from thread context after
       it has proved DMA inactive. */
    (void)g1_bus_gd_command_client_mask();
    operation->command_masked = true;
    operation->command_event = true;
    sem_signal(operation->event);
    thd_schedule(true);
    return true;
}

static bool direct_dma_irq(uint32_t code, void *data) {
    gdrom_direct_dma_operation_t *operation = data;

    if(!operation->active)
        return false;

    operation->dma_code = code;
    operation->dma_event = true;
    if(code != ASIC_EVT_GD_DMA)
        g1_bus_dma_disable();
    sem_signal(operation->event);
    thd_schedule(true);
    return true;
}

static void ata_400ns_delay(void) {
    (void)G1_IN8(G1_ATA_ALTSTATUS);
    (void)G1_IN8(G1_ATA_ALTSTATUS);
    (void)G1_IN8(G1_ATA_ALTSTATUS);
    (void)G1_IN8(G1_ATA_ALTSTATUS);
}

static void rearm_command_irq(gdrom_direct_dma_operation_t *operation) {
    irq_mask_t irq_state;

    /* Packet-request INTRQ has already been acknowledged and no DMA can have
       started yet, so no second event can legitimately arrive while the stale
       wake token is drained. */
    while(sem_trywait(operation->event) == 0) {
    }

    irq_state = irq_disable();
    operation->command_event = false;
    if(operation->command_masked) {
        *(volatile uint32_t *)ASIC_ACK_B = 1u;
        (void)g1_bus_gd_command_client_unmask();
        operation->command_masked = false;
    }
    irq_restore(irq_state);
}

/* This helper is called only with G1 owned and SB_GDST known to be zero. A
   terminal BSY=0/DRQ=0 phase needs only a Status read to deassert INTRQ. A
   command stranded in any other phase is returned to the command-capable
   diagnosis state with the SPI-defined 08h soft reset; ATA SRST is not valid
   for this device. */
static int settle_or_recover_command(bool *command_active,
                                     bool restore_dma_mode,
                                     gdrom_direct_result_t *result) {
    uint64_t deadline;
    uint8_t status;

    if(!*command_active)
        return 0;

    status = G1_IN8(G1_ATA_ALTSTATUS);
    if(!(status & (G1_ATA_SR_BSY | G1_ATA_SR_DRQ))) {
        status = G1_IN8(G1_ATA_STATUS_REG);
        if(result) {
            result->ata_status = status;
            result->interrupt_reason = G1_IN8(G1_ATA_IRQ_REASON);
            if(status & G1_ATA_SR_ERR)
                result->ata_error = G1_IN8(G1_ATA_ERROR);
        }
        *command_active = false;
        return 0;
    }

    if(result)
        result->recovery_attempted = true;
    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_OFF);
    G1_OUT8(G1_ATA_COMMAND_REG, G1_ATA_CMD_SOFT_RESET);
    ata_400ns_delay();

    deadline = timer_ms_gettime64() + GDROM_COMMAND_RECOVERY_MS;
    for(;;) {
        status = G1_IN8(G1_ATA_ALTSTATUS);
        if(!(status & G1_ATA_SR_BSY))
            break;
        if(timer_ms_gettime64() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        thd_pass();
    }

    status = G1_IN8(G1_ATA_STATUS_REG);
    if(result) {
        result->ata_status = status;
        result->interrupt_reason = G1_IN8(G1_ATA_IRQ_REASON);
    }
    if(status & (G1_ATA_SR_BSY | G1_ATA_SR_DRQ)) {
        errno = EIO;
        return -1;
    }

    *command_active = false;
    if(restore_dma_mode
            && set_dma_mode2(timer_ms_gettime64()
                             + GDROM_COMMAND_RECOVERY_MS, result) < 0)
        return -1;
    if(result)
        result->recovery_succeeded = true;
    return 0;
}

static int set_dma_mode2(uint64_t deadline,
                         gdrom_direct_result_t *result) {
    uint8_t status;

    /* CDIF SET FEATURES: feature 03h, transfer-mode byte 22h (WDMA2), then
       command EFh. The zeroed address registers avoid carrying stale packet
       state into implementations that validate the whole task file. */
    G1_OUT8(G1_ATA_FEATURES, G1_ATA_FEATURE_XFER_MODE);
    G1_OUT8(G1_ATA_SECTOR_COUNT, G1_ATA_XFER_WDMA(2));
    G1_OUT8(G1_ATA_LBA_LOW, 0);
    G1_OUT8(G1_ATA_LBA_MID, 0);
    G1_OUT8(G1_ATA_LBA_HIGH, 0);
    G1_OUT8(G1_ATA_COMMAND_REG, G1_ATA_CMD_SET_FEATURES);
    ata_400ns_delay();

    if(wait_next_phase(deadline, &status, result, NULL, NULL) < 0)
        return -1;

    status = G1_IN8(G1_ATA_STATUS_REG);
    if(result) {
        result->ata_status = status;
        if(status & G1_ATA_SR_ERR)
            result->ata_error = G1_IN8(G1_ATA_ERROR);
    }
    if(status & (G1_ATA_SR_BSY | G1_ATA_SR_DRQ)) {
        errno = EPROTO;
        return -1;
    }
    if(status & (G1_ATA_SR_ERR | G1_ATA_SR_DF)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int fail_with_errno(gdrom_direct_result_t *result, int error) {
    if(result) {
        result->ata_status = G1_IN8(G1_ATA_ALTSTATUS);
        /* Command-block registers are invalid while BSY is asserted. */
        if(!(result->ata_status & G1_ATA_SR_BSY)) {
            result->interrupt_reason = G1_IN8(G1_ATA_IRQ_REASON);
            result->device_byte_count =
                (uint16_t)G1_IN8(G1_ATA_LBA_MID)
                | ((uint16_t)G1_IN8(G1_ATA_LBA_HIGH) << 8);
            if(result->ata_status & G1_ATA_SR_ERR)
                result->ata_error = G1_IN8(G1_ATA_ERROR);
        }
    }

    errno = error;
    return -1;
}

static int wait_packet_ready(uint64_t deadline,
                             gdrom_direct_result_t *result,
                             gdrom_direct_cancel_t cancel,
                             void *cancel_data) {
    uint8_t status;

    for(;;) {
        status = G1_IN8(G1_ATA_ALTSTATUS);
        if(result)
            result->ata_status = status;

        if(!(status & G1_ATA_SR_BSY)) {
            if(status & G1_ATA_SR_DRQ)
                return 0;
            if(status & (G1_ATA_SR_ERR | G1_ATA_SR_DF))
                return fail_with_errno(result, EIO);
        }

        if(cancel && cancel(cancel_data))
            return fail_with_errno(result, ECANCELED);

        if(timer_ms_gettime64() >= deadline)
            return fail_with_errno(result, ETIMEDOUT);

        thd_pass();
    }
}

static int wait_next_phase(uint64_t deadline, uint8_t *status_out,
                           gdrom_direct_result_t *result,
                           gdrom_direct_cancel_t cancel,
                           void *cancel_data) {
    uint8_t status;

    for(;;) {
        status = G1_IN8(G1_ATA_ALTSTATUS);
        if(result)
            result->ata_status = status;

        if(!(status & G1_ATA_SR_BSY)) {
            *status_out = status;
            return 0;
        }

        if(cancel && cancel(cancel_data))
            return fail_with_errno(result, ECANCELED);

        if(timer_ms_gettime64() >= deadline)
            return fail_with_errno(result, ETIMEDOUT);

        thd_pass();
    }
}

static void write_packet(const gdrom_spi_packet_t *packet) {
    unsigned int i;

    for(i = 0; i < GDROM_SPI_PACKET_SIZE; i += 2) {
        uint16_t word = (uint16_t)packet->bytes[i]
            | ((uint16_t)packet->bytes[i + 1] << 8);
        G1_OUT16(G1_ATA_DATA, word);
    }
}

static void read_pio_group(uint8_t *buffer, size_t capacity,
                           size_t offset, uint16_t byte_count) {
    size_t i;

    for(i = 0; i < byte_count; i += 2) {
        uint16_t word = G1_IN16(G1_ATA_DATA);

        if(offset + i < capacity)
            buffer[offset + i] = (uint8_t)word;
        if(i + 1 < byte_count && offset + i + 1 < capacity)
            buffer[offset + i + 1] = (uint8_t)(word >> 8);
    }
}

static void write_pio_group(const uint8_t *buffer, size_t size,
                            size_t offset, uint16_t byte_count) {
    size_t i;

    for(i = 0; i < byte_count; i += 2) {
        uint16_t word = 0;

        if(offset + i < size)
            word = buffer[offset + i];
        if(i + 1 < byte_count && offset + i + 1 < size)
            word |= (uint16_t)buffer[offset + i + 1] << 8;
        G1_OUT16(G1_ATA_DATA, word);
    }
}

static int packet_pio_data_in_ex(
        const gdrom_spi_packet_t *packet, void *buffer, size_t capacity,
        uint32_t timeout, gdrom_direct_result_t *result, bool bus_owned,
        bool diagnose_check, gdrom_direct_result_t *error_transport,
        gdrom_direct_cancel_t cancel, void *cancel_data) {
    uint64_t deadline;
    uint64_t now;
    uint8_t *output = buffer;
    uint8_t status;
    uint8_t reason;
    uint16_t byte_count;
    size_t transferred = 0;
    bool overflow = false;
    bool interrupts_disabled = false;
    bool command_active = false;
    bool bus_faulted = false;
    int saved_errno = 0;
    int rv = -1;

    if(!packet || (!buffer && capacity) || !timeout || capacity > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }

    if(result) {
        memset(result, 0, sizeof(*result));
        result->phase = GDROM_DIRECT_PHASE_WAIT_IDLE;
    }

    deadline = timer_ms_gettime64() + timeout;
    if(!bus_owned && g1_bus_lock_timed(timeout) < 0)
        return -1;
    if(cancel && cancel(cancel_data)) {
        errno = ECANCELED;
        goto out;
    }

    G1_OUT32(G1_ATA_PIO_RACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);
    G1_OUT32(G1_ATA_PIO_WACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);

    /* All BIOS-backed GD-ROM and ATA operations use this same ownership gate.
       With G1 idle, select the master and suppress polled-command interrupts. */
    now = timer_ms_gettime64();
    timeout = now < deadline ? (uint32_t)(deadline - now) : 0;
    if(!timeout || g1_bus_select_device_timed(0, timeout, NULL) < 0) {
        if(!timeout)
            errno = ETIMEDOUT;
        goto out;
    }
    now = timer_ms_gettime64();
    timeout = now < deadline ? (uint32_t)(deadline - now) : 0;
    if(!timeout) {
        errno = ETIMEDOUT;
        goto out;
    }
    if(g1_bus_wait_status(0, G1_ATA_SR_BSY | G1_ATA_SR_DRQ,
                          timeout, &status) < 0)
        goto out;

    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_OFF);
    interrupts_disabled = true;
    (void)G1_IN8(G1_ATA_STATUS_REG); /* Clear a stale device INTRQ. */

    G1_OUT8(G1_ATA_FEATURES, 0); /* PIO transfer. */
    G1_OUT8(G1_ATA_LBA_LOW, 0);
    G1_OUT8(G1_ATA_LBA_MID, (uint8_t)capacity);
    G1_OUT8(G1_ATA_LBA_HIGH, (uint8_t)(capacity >> 8));
    G1_OUT8(G1_ATA_COMMAND_REG, G1_ATA_CMD_PACKET);
    command_active = true;
    ata_400ns_delay();

    if(result)
        result->phase = GDROM_DIRECT_PHASE_WAIT_PACKET;
    if(wait_packet_ready(deadline, result, cancel, cancel_data) < 0)
        goto out;

    status = G1_IN8(G1_ATA_STATUS_REG); /* Acknowledge packet-out INTRQ. */
    if(!(status & G1_ATA_SR_DRQ)) {
        fail_with_errno(result, EPROTO);
        goto out;
    }
    reason = G1_IN8(G1_ATA_IRQ_REASON);
    if(result) {
        result->ata_status = status;
        result->interrupt_reason = reason;
    }
    if((reason & GDROM_REASON_MASK) != GDROM_REASON_PACKET_OUT) {
        fail_with_errno(result, EPROTO);
        goto out;
    }

    write_packet(packet);
    ata_400ns_delay();

    for(;;) {
        if(wait_next_phase(deadline, &status, result,
                           cancel, cancel_data) < 0)
            goto out;

        if(!(status & G1_ATA_SR_DRQ))
            break;

        status = G1_IN8(G1_ATA_STATUS_REG); /* Acknowledge this data INTRQ. */
        reason = G1_IN8(G1_ATA_IRQ_REASON);
        byte_count = (uint16_t)G1_IN8(G1_ATA_LBA_MID)
            | ((uint16_t)G1_IN8(G1_ATA_LBA_HIGH) << 8);

        if(result) {
            result->phase = GDROM_DIRECT_PHASE_DATA_IN;
            result->ata_status = status;
            result->interrupt_reason = reason;
            result->device_byte_count = byte_count;
        }

        if((reason & GDROM_REASON_MASK) != GDROM_REASON_DATA_IN
                || !byte_count) {
            fail_with_errno(result, EPROTO);
            goto out;
        }

        read_pio_group(output, capacity, transferred, byte_count);
        if(byte_count > capacity - (transferred < capacity
                                     ? transferred : capacity))
            overflow = true;
        transferred += byte_count;
        if(result)
            result->transferred = transferred;
        ata_400ns_delay();
    }

    status = G1_IN8(G1_ATA_STATUS_REG); /* Acknowledge final status INTRQ. */
    reason = G1_IN8(G1_ATA_IRQ_REASON);
    command_active = false;
    if(result) {
        result->phase = GDROM_DIRECT_PHASE_COMPLETE;
        result->ata_status = status;
        result->interrupt_reason = reason;
        result->transferred = transferred;
        if(status & G1_ATA_SR_ERR)
            result->ata_error = G1_IN8(G1_ATA_ERROR);
    }

    if(status & (G1_ATA_SR_ERR | G1_ATA_SR_DF)) {
        errno = EIO;
        goto out;
    }
    if((reason & GDROM_REASON_MASK) != GDROM_REASON_STATUS) {
        errno = EPROTO;
        goto out;
    }
    if(overflow) {
        errno = EMSGSIZE;
        goto out;
    }

    rv = 0;

out:
    if(rv < 0)
        saved_errno = errno;
    if(command_active
            && settle_or_recover_command(&command_active, false,
                                         result) < 0) {
        g1_bus_mark_faulted();
        bus_faulted = true;
    }
    if(rv < 0 && saved_errno == EIO && diagnose_check && result
            && (result->ata_status & G1_ATA_SR_ERR) && !bus_faulted)
        (void)capture_check_sense_locked(result, error_transport);
    if(interrupts_disabled && !bus_faulted)
        G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_ON);
    if(!bus_owned && !bus_faulted)
        g1_bus_unlock();
    if(rv < 0)
        errno = saved_errno;
    return rv;
}

static int packet_pio_data_in(const gdrom_spi_packet_t *packet,
                              void *buffer, size_t capacity,
                              uint32_t timeout,
                              gdrom_direct_result_t *result) {
    return packet_pio_data_in_ex(packet, buffer, capacity, timeout, result,
                                 false, true, NULL, NULL, NULL);
}

static int packet_pio_data_out_ex(
        const gdrom_spi_packet_t *packet, const void *buffer, size_t size,
        uint32_t timeout, gdrom_direct_result_t *result, bool bus_owned,
        bool diagnose_check, gdrom_direct_result_t *error_transport,
        gdrom_direct_cancel_t cancel, void *cancel_data) {
    uint64_t deadline;
    uint64_t now;
    const uint8_t *input = buffer;
    uint8_t status;
    uint8_t reason;
    uint16_t byte_count;
    size_t transferred = 0;
    bool overflow = false;
    bool interrupts_disabled = false;
    bool command_active = false;
    bool bus_faulted = false;
    int saved_errno = 0;
    int rv = -1;

    if(!packet || (!buffer && size) || !timeout || size > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }

    if(result) {
        memset(result, 0, sizeof(*result));
        result->phase = GDROM_DIRECT_PHASE_WAIT_IDLE;
    }

    deadline = timer_ms_gettime64() + timeout;
    if(!bus_owned && g1_bus_lock_timed(timeout) < 0)
        return -1;
    if(cancel && cancel(cancel_data)) {
        errno = ECANCELED;
        goto out;
    }

    G1_OUT32(G1_ATA_PIO_RACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);
    G1_OUT32(G1_ATA_PIO_WACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);

    now = timer_ms_gettime64();
    timeout = now < deadline ? (uint32_t)(deadline - now) : 0;
    if(!timeout || g1_bus_select_device_timed(0, timeout, NULL) < 0) {
        if(!timeout)
            errno = ETIMEDOUT;
        goto out;
    }
    now = timer_ms_gettime64();
    timeout = now < deadline ? (uint32_t)(deadline - now) : 0;
    if(!timeout) {
        errno = ETIMEDOUT;
        goto out;
    }
    if(g1_bus_wait_status(0, G1_ATA_SR_BSY | G1_ATA_SR_DRQ,
                          timeout, &status) < 0)
        goto out;

    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_OFF);
    interrupts_disabled = true;
    (void)G1_IN8(G1_ATA_STATUS_REG);

    G1_OUT8(G1_ATA_FEATURES, 0);
    G1_OUT8(G1_ATA_LBA_LOW, 0);
    G1_OUT8(G1_ATA_LBA_MID, (uint8_t)size);
    G1_OUT8(G1_ATA_LBA_HIGH, (uint8_t)(size >> 8));
    G1_OUT8(G1_ATA_COMMAND_REG, G1_ATA_CMD_PACKET);
    command_active = true;
    ata_400ns_delay();

    if(result)
        result->phase = GDROM_DIRECT_PHASE_WAIT_PACKET;
    if(wait_packet_ready(deadline, result, cancel, cancel_data) < 0)
        goto out;

    status = G1_IN8(G1_ATA_STATUS_REG);
    if(!(status & G1_ATA_SR_DRQ)) {
        fail_with_errno(result, EPROTO);
        goto out;
    }
    reason = G1_IN8(G1_ATA_IRQ_REASON);
    if(result) {
        result->ata_status = status;
        result->interrupt_reason = reason;
    }
    if((reason & GDROM_REASON_MASK) != GDROM_REASON_PACKET_OUT) {
        fail_with_errno(result, EPROTO);
        goto out;
    }

    write_packet(packet);
    ata_400ns_delay();

    for(;;) {
        if(wait_next_phase(deadline, &status, result,
                           cancel, cancel_data) < 0)
            goto out;

        if(!(status & G1_ATA_SR_DRQ))
            break;

        status = G1_IN8(G1_ATA_STATUS_REG);
        reason = G1_IN8(G1_ATA_IRQ_REASON);
        byte_count = (uint16_t)G1_IN8(G1_ATA_LBA_MID)
            | ((uint16_t)G1_IN8(G1_ATA_LBA_HIGH) << 8);

        if(result) {
            result->phase = GDROM_DIRECT_PHASE_DATA_OUT;
            result->ata_status = status;
            result->interrupt_reason = reason;
            result->device_byte_count = byte_count;
        }

        if((reason & GDROM_REASON_MASK) != GDROM_REASON_DATA_OUT
                || !byte_count) {
            fail_with_errno(result, EPROTO);
            goto out;
        }

        write_pio_group(input, size, transferred, byte_count);
        if(byte_count > size - (transferred < size ? transferred : size))
            overflow = true;
        transferred += byte_count;
        if(result)
            result->transferred = transferred;
        ata_400ns_delay();
    }

    status = G1_IN8(G1_ATA_STATUS_REG);
    reason = G1_IN8(G1_ATA_IRQ_REASON);
    command_active = false;
    if(result) {
        result->phase = GDROM_DIRECT_PHASE_COMPLETE;
        result->ata_status = status;
        result->interrupt_reason = reason;
        result->transferred = transferred;
        if(status & G1_ATA_SR_ERR)
            result->ata_error = G1_IN8(G1_ATA_ERROR);
    }

    if(status & (G1_ATA_SR_ERR | G1_ATA_SR_DF)) {
        errno = EIO;
        goto out;
    }
    if((reason & GDROM_REASON_MASK) != GDROM_REASON_STATUS) {
        errno = EPROTO;
        goto out;
    }
    if(overflow || transferred != size) {
        errno = EMSGSIZE;
        goto out;
    }

    rv = 0;

out:
    if(rv < 0)
        saved_errno = errno;
    if(command_active
            && settle_or_recover_command(&command_active, false,
                                         result) < 0) {
        g1_bus_mark_faulted();
        bus_faulted = true;
    }
    if(rv < 0 && saved_errno == EIO && diagnose_check && result
            && (result->ata_status & G1_ATA_SR_ERR) && !bus_faulted)
        (void)capture_check_sense_locked(result, error_transport);
    if(interrupts_disabled && !bus_faulted)
        G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_ON);
    if(!bus_owned && !bus_faulted)
        g1_bus_unlock();
    if(rv < 0)
        errno = saved_errno;
    return rv;
}

static int test_unit_internal(uint32_t timeout,
                              gdrom_direct_result_t *result,
                              gdrom_direct_result_t *error_transport,
                              bool bus_owned, gdrom_direct_cancel_t cancel,
                              void *cancel_data) {
    gdrom_spi_packet_t packet;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;

    gdrom_spi_test_unit(&packet);
    return packet_pio_data_in_ex(&packet, NULL, 0, timeout, observed,
                                 bus_owned, true, error_transport,
                                 cancel, cancel_data);
}

int gdrom_direct_test_unit(uint32_t timeout, gdrom_direct_result_t *result) {
    return test_unit_internal(timeout, result, NULL, false, NULL, NULL);
}

static int get_error_internal(gdrom_direct_error_t *error, uint32_t timeout,
                              gdrom_direct_result_t *result,
                              bool bus_owned) {
    gdrom_spi_packet_t packet;
    gdrom_spi_error_t decoded;
    uint8_t response[GDROM_SPI_ERROR_SIZE];
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;
    int saved_errno = 0;
    int rv;

    if(!error || !timeout) {
        errno = EINVAL;
        return -1;
    }

    gdrom_spi_req_error(&packet, sizeof(response));
    rv = packet_pio_data_in_ex(&packet, response, sizeof(response), timeout,
                               observed, bus_owned, false, NULL, NULL, NULL);
    if(rv < 0)
        saved_errno = errno;

    /* A complete response remains diagnostic data even if final status also
       carries CHECK. Preserve that command result after decoding the bytes. */
    if(observed->transferred != sizeof(response)) {
        if(rv < 0) {
            errno = saved_errno;
            return -1;
        }
        errno = EPROTO;
        return -1;
    }
    if(gdrom_spi_decode_error(response, sizeof(response), &decoded) != 0) {
        errno = EPROTO;
        return -1;
    }

    error->sense.key = (cdrom_sense_key_t)decoded.sense_key;
    error->sense.asc = decoded.asc;
    error->sense.ascq = decoded.ascq;
    error->command_specific_information =
        decoded.command_specific_information;

    if(rv < 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int gdrom_direct_get_error(gdrom_direct_error_t *error, uint32_t timeout,
                           gdrom_direct_result_t *result) {
    return get_error_internal(error, timeout, result, false);
}

static int capture_check_sense_locked(
        gdrom_direct_result_t *result,
        gdrom_direct_result_t *error_transport) {
    gdrom_direct_error_t error;
    gdrom_direct_result_t local_transport;
    gdrom_direct_result_t *observed = error_transport
        ? error_transport : &local_transport;
    int command_errno;
    int rv;

    rv = get_error_internal(&error, GDROM_COMMAND_RECOVERY_MS, observed, true);
    command_errno = rv < 0 ? errno : 0;
    if(rv < 0 && !(command_errno == EIO
            && observed->phase == GDROM_DIRECT_PHASE_COMPLETE
            && observed->transferred == GDROM_SPI_ERROR_SIZE)) {
        errno = command_errno;
        return -1;
    }

    result->sense_valid = true;
    result->sense = error.sense;
    result->command_specific_information =
        error.command_specific_information;
    return 0;
}

static int get_status_internal(gdrom_direct_status_t *status,
                               uint32_t timeout,
                               gdrom_direct_result_t *result,
                               gdrom_direct_result_t *error_transport,
                               bool bus_owned, gdrom_direct_cancel_t cancel,
                               void *cancel_data) {
    gdrom_spi_packet_t packet;
    gdrom_spi_status_t decoded;
    uint8_t response[GDROM_SPI_STATUS_SIZE];
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;
    int saved_errno = 0;
    int rv;

    if(!status || !timeout) {
        errno = EINVAL;
        return -1;
    }

    if(gdrom_spi_req_stat(&packet, 0, sizeof(response)) != 0) {
        errno = EINVAL;
        return -1;
    }

    rv = packet_pio_data_in_ex(&packet, response, sizeof(response), timeout,
                               observed, bus_owned, true, error_transport,
                               cancel, cancel_data);
    if(rv < 0)
        saved_errno = errno;

    /* REQ_STAT can return a complete status payload followed by CHECK. Decode
       that payload for diagnostic callers, then preserve the EIO return. */
    if(observed->transferred != sizeof(response)) {
        if(rv < 0) {
            errno = saved_errno;
            return -1;
        }
        errno = EPROTO;
        return -1;
    }

    if(gdrom_spi_decode_status(response, sizeof(response), &decoded) != 0) {
        errno = EPROTO;
        return -1;
    }

    status->status = (cd_stat_t)decoded.status;
    status->disc_type = (cd_disc_types_t)decoded.disc_format;
    status->repeat_count = decoded.repeat_count;
    status->control = decoded.control;
    status->adr = decoded.adr;
    status->track = decoded.track;
    status->index = decoded.index;
    status->fad = decoded.fad;
    status->max_read_retries = decoded.max_read_retries;

    if(rv < 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int gdrom_direct_get_status(gdrom_direct_status_t *status, uint32_t timeout,
                            gdrom_direct_result_t *result) {
    return get_status_internal(status, timeout, result, NULL, false,
                               NULL, NULL);
}

static void copy_mode(gdrom_direct_mode_t *mode,
                      const gdrom_spi_mode_t *decoded) {
    memset(mode, 0, sizeof(*mode));
    mode->settings.speed = (gdrom_direct_speed_t)decoded->speed;
    mode->settings.standby_seconds = decoded->standby_seconds;
    mode->settings.read_continuous = decoded->read_continuous;
    mode->settings.ecc_retry = decoded->ecc_retry;
    mode->settings.read_retry = decoded->read_retry;
    mode->settings.form2_retry = decoded->form2_retry;
    mode->settings.read_retry_count = decoded->read_retry_count;
    memcpy(mode->drive_information, decoded->drive_information,
           sizeof(decoded->drive_information));
    memcpy(mode->system_version, decoded->system_version,
           sizeof(decoded->system_version));
    memcpy(mode->system_date, decoded->system_date,
           sizeof(decoded->system_date));
}

static int get_mode_spi_internal(
        gdrom_spi_mode_t *mode, uint32_t timeout,
        gdrom_direct_result_t *result, bool bus_owned,
        gdrom_direct_cancel_t cancel, void *cancel_data) {
    gdrom_spi_packet_t packet;
    uint8_t response[GDROM_SPI_MODE_SIZE];
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;

    if(!mode || !timeout
            || gdrom_spi_req_mode(&packet, 0, sizeof(response)) != 0) {
        errno = EINVAL;
        return -1;
    }

    if(packet_pio_data_in_ex(&packet, response, sizeof(response), timeout,
                             observed, bus_owned, true, NULL,
                             cancel, cancel_data) < 0)
        return -1;
    if(observed->transferred != sizeof(response)
            || gdrom_spi_decode_mode(response, sizeof(response), mode) != 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int get_mode_internal(
        gdrom_direct_mode_t *mode, uint32_t timeout,
        gdrom_direct_result_t *result, gdrom_direct_cancel_t cancel,
        void *cancel_data) {
    gdrom_spi_mode_t decoded;

    if(!mode) {
        errno = EINVAL;
        return -1;
    }
    if(get_mode_spi_internal(&decoded, timeout, result, false,
                             cancel, cancel_data) < 0)
        return -1;
    copy_mode(mode, &decoded);
    return 0;
}

int gdrom_direct_get_mode(gdrom_direct_mode_t *mode, uint32_t timeout,
                          gdrom_direct_result_t *result) {
    return get_mode_internal(mode, timeout, result, NULL, NULL);
}

static bool valid_mode_settings(
        const gdrom_direct_mode_settings_t *settings) {
    return settings && (unsigned int)settings->speed <= 7u;
}

static int set_mode_internal(
        const gdrom_direct_mode_settings_t *settings, uint32_t timeout,
        gdrom_direct_result_t *result, gdrom_direct_cancel_t cancel,
        void *cancel_data) {
    gdrom_spi_packet_t packet;
    gdrom_spi_mode_t current;
    uint8_t data[GDROM_SPI_MODE_WRITABLE_SIZE];
    uint64_t deadline;
    uint32_t remaining;
    int saved_errno = 0;
    int rv = -1;

    if(!valid_mode_settings(settings) || !timeout) {
        errno = EINVAL;
        return -1;
    }

    deadline = timer_ms_gettime64() + timeout;
    if(g1_bus_lock_timed(timeout) < 0)
        return -1;
    if(cancel && cancel(cancel_data)) {
        errno = ECANCELED;
        goto out;
    }

    if(deadline_timeout(deadline, &remaining) < 0
            || get_mode_spi_internal(&current, remaining, result, true,
                                     cancel, cancel_data) < 0)
        goto out;

    current.speed = (uint8_t)settings->speed;
    current.standby_seconds = settings->standby_seconds;
    current.read_continuous = settings->read_continuous;
    current.ecc_retry = settings->ecc_retry;
    current.read_retry = settings->read_retry;
    current.form2_retry = settings->form2_retry;
    current.read_retry_count = settings->read_retry_count;
    if(gdrom_spi_encode_mode_writable(data, &current) != 0
            || gdrom_spi_set_mode(&packet, GDROM_SPI_MODE_WRITABLE_START,
                                  sizeof(data)) != 0) {
        errno = EINVAL;
        goto out;
    }

    if(deadline_timeout(deadline, &remaining) < 0)
        goto out;
    rv = packet_pio_data_out_ex(&packet, data, sizeof(data), remaining,
                                result, true, true, NULL,
                                cancel, cancel_data);

out:
    if(rv < 0)
        saved_errno = errno;
    if(!g1_bus_is_faulted())
        g1_bus_unlock();
    if(rv < 0)
        errno = saved_errno;
    return rv;
}

int gdrom_direct_set_mode(const gdrom_direct_mode_settings_t *settings,
                          uint32_t timeout,
                          gdrom_direct_result_t *result) {
    return set_mode_internal(settings, timeout, result, NULL, NULL);
}

int gdrom_direct_get_status_locked(
        gdrom_direct_status_t *status, uint32_t timeout,
        gdrom_direct_result_t *result) {
    return get_status_internal(status, timeout, result, NULL, true,
                               NULL, NULL);
}

static int seek_internal(uint32_t fad, uint32_t timeout,
                         gdrom_direct_result_t *result,
                         gdrom_direct_cancel_t cancel, void *cancel_data) {
    gdrom_spi_packet_t packet;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;

    if(fad < 150u || fad > GDROM_SPI_MAX_U24 || !timeout
            || gdrom_spi_seek(&packet, GDROM_SPI_SEEK_FAD, fad) != 0) {
        errno = EINVAL;
        return -1;
    }

    return packet_pio_data_in_ex(&packet, NULL, 0, timeout, observed, false,
                                 true, NULL, cancel, cancel_data);
}

int gdrom_direct_seek(uint32_t fad, uint32_t timeout,
                      gdrom_direct_result_t *result) {
    return seek_internal(fad, timeout, result, NULL, NULL);
}

static int deadline_timeout(uint64_t deadline, uint32_t *timeout) {
    uint64_t now = timer_ms_gettime64();

    if(now >= deadline) {
        errno = ETIMEDOUT;
        return -1;
    }

    *timeout = (uint32_t)(deadline - now);
    return 0;
}

static bool completed_check(const gdrom_direct_result_t *transport,
                            size_t expected_bytes, int command_errno) {
    return command_errno == EIO
        && transport->phase == GDROM_DIRECT_PHASE_COMPLETE
        && transport->transferred == expected_bytes;
}

static int probe_accept_captured_error(
        gdrom_direct_probe_result_t *probe,
        const gdrom_direct_result_t *transport) {
    if(!transport->sense_valid) {
        errno = EPROTO;
        return -1;
    }

    probe->last_command = GDROM_DIRECT_PROBE_REQ_ERROR;
    ++probe->error_requests;
    probe->error_valid = true;
    probe->error.sense = transport->sense;
    probe->error.command_specific_information =
        transport->command_specific_information;
    probe->result = cdrom_sense_to_result(&probe->error.sense);
    return 0;
}

static int probe_internal(
        gdrom_direct_probe_result_t *probe, uint32_t timeout,
        bool bus_owned, gdrom_direct_cancel_t cancel, void *cancel_data) {
    uint64_t deadline;
    uint32_t remaining;
    int command_errno;
    int rv;
    unsigned int attempt;

    if(!probe || !timeout) {
        errno = EINVAL;
        return -1;
    }

    memset(probe, 0, sizeof(*probe));
    probe->result = ERR_OK;
    deadline = timer_ms_gettime64() + timeout;

    if(deadline_timeout(deadline, &remaining) < 0)
        return -1;
    probe->last_command = GDROM_DIRECT_PROBE_TEST_UNIT;
    rv = test_unit_internal(remaining, &probe->test_unit_transport,
                            &probe->error_transport, bus_owned,
                            cancel, cancel_data);
    command_errno = rv < 0 ? errno : 0;
    if(rv < 0) {
        if(!completed_check(&probe->test_unit_transport, 0,
                            command_errno)) {
            errno = command_errno;
            return -1;
        }
        if(probe_accept_captured_error(
                probe, &probe->test_unit_transport) < 0)
            return -1;
    }

    /* One retry is enough to observe the state after REQ_ERROR acknowledges
       an initial unit-attention or not-ready CHECK. */
    for(attempt = 0; attempt < 2; ++attempt) {
        if(deadline_timeout(deadline, &remaining) < 0)
            return -1;

        probe->last_command = GDROM_DIRECT_PROBE_REQ_STAT;
        ++probe->status_requests;
        rv = get_status_internal(&probe->status, remaining,
                                 &probe->status_transport,
                                 &probe->error_transport, bus_owned,
                                 cancel, cancel_data);
        command_errno = rv < 0 ? errno : 0;
        if(rv == 0) {
            probe->status_valid = true;
            break;
        }
        if(!completed_check(&probe->status_transport,
                            GDROM_SPI_STATUS_SIZE, command_errno)) {
            errno = command_errno;
            return -1;
        }

        probe->status_valid = true;
        if(probe_accept_captured_error(
                probe, &probe->status_transport) < 0)
            return -1;
    }

    if(!probe->status_valid) {
        errno = EPROTO;
        return -1;
    }

    /* Current drive state is more specific than a preceding unit-attention
       notification when it says the drive cannot service media commands.
       In particular, FATAL means an explicit reset is required; reporting it
       as ERR_OK would send callers directly into a doomed TOC/read command. */
    switch(probe->status.status) {
        case CD_STATUS_OPEN:
        case CD_STATUS_NO_DISC:
            probe->result = ERR_NO_DISC;
            break;
        case CD_STATUS_BUSY:
            probe->result = ERR_BUSY;
            break;
        case CD_STATUS_RETRY:
            probe->result = ERR_NOT_READY;
            break;
        case CD_STATUS_READ_FAIL:
        case CD_STATUS_ERROR:
        case CD_STATUS_FATAL:
            probe->result = ERR_SYS;
            break;
        default:
            if(!probe->error_valid)
                probe->result = ERR_OK;
            break;
    }

    errno = 0;
    return 0;
}

int gdrom_direct_probe(gdrom_direct_probe_result_t *probe, uint32_t timeout) {
    return probe_internal(probe, timeout, false, NULL, NULL);
}

static int reinitialize_internal(
        gdrom_direct_reinit_result_t *reinit, uint32_t timeout,
        gdrom_direct_cancel_t cancel, void *cancel_data) {
    uint64_t deadline;
    uint32_t remaining;
    uint8_t status;
    int saved_errno = 0;
    int rv = -1;

    if(!reinit || !timeout) {
        errno = EINVAL;
        return -1;
    }

    memset(reinit, 0, sizeof(*reinit));
    deadline = timer_ms_gettime64() + timeout;
    if(g1_bus_lock_timed(timeout) < 0)
        return -1;
    if(cancel && cancel(cancel_data)) {
        errno = ECANCELED;
        goto out;
    }

    G1_OUT32(G1_ATA_PIO_RACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);
    G1_OUT32(G1_ATA_PIO_WACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);
    if(deadline_timeout(deadline, &remaining) < 0
            || g1_bus_select_device_timed(0, remaining, NULL) < 0)
        goto out;

    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_OFF);
    (void)G1_IN8(G1_ATA_STATUS_REG);
    reinit->reset_transport.phase = GDROM_DIRECT_PHASE_WAIT_IDLE;
    reinit->reset_transport.recovery_attempted = true;
    G1_OUT8(G1_ATA_COMMAND_REG, G1_ATA_CMD_SOFT_RESET);
    ata_400ns_delay();

    /* SPI soft reset is itself the cancellation/recovery boundary. Once the
       08h command has been issued, finish its bounded BSY handshake before
       honoring cancellation so G1 is never published in a half-reset state. */
    for(;;) {
        status = G1_IN8(G1_ATA_ALTSTATUS);
        reinit->reset_transport.ata_status = status;
        if(!(status & G1_ATA_SR_BSY))
            break;
        if(timer_ms_gettime64() >= deadline) {
            errno = ETIMEDOUT;
            g1_bus_mark_faulted();
            goto out;
        }
        thd_pass();
    }

    status = G1_IN8(G1_ATA_STATUS_REG);
    reinit->reset_transport.phase = GDROM_DIRECT_PHASE_COMPLETE;
    reinit->reset_transport.ata_status = status;
    reinit->reset_transport.interrupt_reason =
        G1_IN8(G1_ATA_IRQ_REASON);
    if(status & G1_ATA_SR_ERR)
        reinit->reset_transport.ata_error = G1_IN8(G1_ATA_ERROR);
    if(status & (G1_ATA_SR_BSY | G1_ATA_SR_DRQ)) {
        errno = EPROTO;
        g1_bus_mark_faulted();
        goto out;
    }
    reinit->reset_transport.recovery_succeeded = true;
    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_ON);

    if(cancel && cancel(cancel_data)) {
        errno = ECANCELED;
        goto out;
    }
    if(deadline_timeout(deadline, &remaining) < 0)
        goto out;
    rv = probe_internal(&reinit->probe, remaining, true,
                        cancel, cancel_data);

out:
    if(rv < 0)
        saved_errno = errno;
    if(!g1_bus_is_faulted()) {
        G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_ON);
        g1_bus_unlock();
    }
    if(rv < 0)
        errno = saved_errno;
    return rv;
}

int gdrom_direct_reinitialize(gdrom_direct_reinit_result_t *result,
                              uint32_t timeout) {
    return reinitialize_internal(result, timeout, NULL, NULL);
}

static int read_toc_internal(
        cd_toc_t *toc, bool high_density, uint32_t timeout,
        gdrom_direct_result_t *result, bool bus_owned,
        gdrom_direct_cancel_t cancel, void *cancel_data) {
    gdrom_spi_packet_t packet;
    gdrom_spi_toc_t decoded;
    uint8_t response[GDROM_SPI_TOC_SIZE];
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;
    size_t i;

    if(!toc || !timeout) {
        errno = EINVAL;
        return -1;
    }

    gdrom_spi_get_toc(&packet, high_density, sizeof(response));
    if(packet_pio_data_in_ex(&packet, response, sizeof(response), timeout,
                             observed, bus_owned, true, NULL,
                             cancel, cancel_data) < 0)
        return -1;
    if(observed->transferred != sizeof(response)
            || gdrom_spi_decode_toc(response, sizeof(response), &decoded)
                != 0) {
        errno = EPROTO;
        return -1;
    }

    for(i = 0; i < 99; ++i)
        toc->entry[i] = decoded.entry[i];
    toc->first = decoded.first;
    toc->last = decoded.last;
    toc->leadout_sector = decoded.leadout;
    return 0;
}

int gdrom_direct_read_toc(cd_toc_t *toc, bool high_density,
                          uint32_t timeout, gdrom_direct_result_t *result) {
    return read_toc_internal(toc, high_density, timeout, result,
                             false, NULL, NULL);
}

static bool valid_cdda_play_arguments(uint32_t start, uint32_t end,
                                      uint32_t loops, int mode,
                                      uint32_t timeout) {
    if(!timeout || loops > 0x0fu)
        return false;

    if(mode == CDDA_TRACKS)
        return start >= 1u && start <= 99u && end >= start && end <= 99u;

    if(mode == CDDA_SECTORS)
        return start >= 150u && start <= GDROM_SPI_MAX_U24
            && end <= GDROM_SPI_MAX_U24 && (!end || end >= start);

    return false;
}

static int get_subcode_internal(
        void *buffer, size_t buflen, cd_sub_type_t which, uint32_t timeout,
        gdrom_direct_result_t *result, gdrom_direct_cancel_t cancel,
        void *cancel_data) {
    gdrom_spi_packet_t packet;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;

    if(!buffer || !buflen || buflen > UINT16_MAX || !timeout
            || which < CD_SUB_Q_ALL || which > CD_SUB_TRACK_ISRC
            || gdrom_spi_get_subcode(
                &packet, (gdrom_spi_subcode_format_t)which,
                (uint16_t)buflen) != 0) {
        errno = EINVAL;
        return -1;
    }

    if(packet_pio_data_in_ex(&packet, buffer, buflen, timeout, observed,
                             false, true, NULL, cancel, cancel_data) < 0)
        return -1;
    if(observed->transferred != buflen) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int gdrom_direct_get_subcode(
        void *buffer, size_t buflen, cd_sub_type_t which, uint32_t timeout,
        gdrom_direct_result_t *result) {
    return get_subcode_internal(buffer, buflen, which, timeout, result,
                                NULL, NULL);
}

static int cdda_status_internal(
        cdrom_cdda_status_t *status, uint32_t timeout,
        gdrom_direct_result_t *result, gdrom_direct_cancel_t cancel,
        void *cancel_data) {
    uint8_t subcode[14];

    if(!status) {
        errno = EINVAL;
        return -1;
    }
    if(get_subcode_internal(subcode, sizeof(subcode), CD_SUB_Q_CHANNEL,
                            timeout, result, cancel, cancel_data) < 0)
        return -1;

    cdrom_decode_cdda_status_internal(subcode, status);
    return 0;
}

int gdrom_direct_cdda_get_status(
        cdrom_cdda_status_t *status, uint32_t timeout,
        gdrom_direct_result_t *result) {
    return cdda_status_internal(status, timeout, result, NULL, NULL);
}

static int cdda_play_internal(
        uint32_t start, uint32_t end, uint32_t loops, int mode,
        uint32_t timeout, gdrom_direct_result_t *result,
        gdrom_direct_cancel_t cancel, void *cancel_data) {
    gdrom_spi_packet_t packet;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;

    if(!valid_cdda_play_arguments(start, end, loops, mode, timeout)) {
        errno = EINVAL;
        return -1;
    }

    if(mode == CDDA_SECTORS) {
        if(gdrom_spi_play(&packet, GDROM_SPI_POINT_FAD,
                          start, end, (uint8_t)loops) != 0) {
            errno = EINVAL;
            return -1;
        }
        return packet_pio_data_in_ex(&packet, NULL, 0, timeout, observed,
                                     false, true, NULL,
                                     cancel, cancel_data);
    }
    else {
        cd_toc_t toc;
        uint64_t deadline = timer_ms_gettime64() + timeout;
        uint32_t remaining;
        uint32_t first;
        uint32_t last;
        uint32_t start_fad;
        uint32_t end_fad;
        int saved_errno;
        int rv = -1;

        /* SPI CD_PLAY has FAD/MSF forms only. Resolve the track-number form
           exactly once while retaining G1 so no intervening drive command can
           invalidate the TOC-to-FAD range before playback begins. */
        if(g1_bus_lock_timed(timeout) < 0)
            return -1;
        if(cancel && cancel(cancel_data)) {
            errno = ECANCELED;
            goto out_tracks;
        }
        if(deadline_timeout(deadline, &remaining) < 0
                || read_toc_internal(&toc, false, remaining, observed,
                                     true, cancel, cancel_data) < 0)
            goto out_tracks;

        first = TOC_TRACK(toc.first);
        last = TOC_TRACK(toc.last);
        if((first < 1u || last > 99u || first > last
                || start < first || end > last)) {
            /* GD-DA tracks live in the high-density program area. The
               single-density TOC is still checked first so ordinary mixed-
               mode CDs need only one command. A play range may not cross the
               two physically discontinuous areas. */
            if(deadline_timeout(deadline, &remaining) < 0
                    || read_toc_internal(&toc, true, remaining, observed,
                                         true, cancel, cancel_data) < 0)
                goto out_tracks;
            first = TOC_TRACK(toc.first);
            last = TOC_TRACK(toc.last);
        }
        if(first < 1u || last > 99u || first > last
                || start < first || end > last
                || toc.entry[start - 1u] == 0xffffffffu
                || (end < last && toc.entry[end] == 0xffffffffu)) {
            errno = ENOENT;
            goto out_tracks;
        }

        start_fad = TOC_LBA(toc.entry[start - 1u]);
        end_fad = end < last ? TOC_LBA(toc.entry[end])
                             : TOC_LBA(toc.leadout_sector);
        if(start_fad < 150u || start_fad > GDROM_SPI_MAX_U24
                || end_fad < start_fad || end_fad > GDROM_SPI_MAX_U24
                || gdrom_spi_play(&packet, GDROM_SPI_POINT_FAD,
                                  start_fad, end_fad,
                                  (uint8_t)loops) != 0) {
            errno = EPROTO;
            goto out_tracks;
        }
        if(deadline_timeout(deadline, &remaining) < 0)
            goto out_tracks;
        rv = packet_pio_data_in_ex(&packet, NULL, 0, remaining, observed,
                                   true, true, NULL,
                                   cancel, cancel_data);

out_tracks:
        saved_errno = rv < 0 ? errno : 0;
        if(!g1_bus_is_faulted())
            g1_bus_unlock();
        if(rv < 0)
            errno = saved_errno;
        return rv;
    }
}

int gdrom_direct_cdda_play(
        uint32_t start, uint32_t end, uint32_t loops, int mode,
        uint32_t timeout, gdrom_direct_result_t *result) {
    return cdda_play_internal(start, end, loops, mode, timeout, result,
                              NULL, NULL);
}

static int cdda_simple_internal(
        gdrom_direct_cdda_operation_t operation, bool reverse, uint8_t speed,
        uint32_t timeout, gdrom_direct_result_t *result,
        gdrom_direct_cancel_t cancel, void *cancel_data) {
    gdrom_spi_packet_t packet;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;

    if(!timeout) {
        errno = EINVAL;
        return -1;
    }

    switch(operation) {
        case GDROM_DIRECT_CDDA_PAUSE:
            if(gdrom_spi_seek(&packet, GDROM_SPI_SEEK_PAUSE, 0) != 0) {
                errno = EINVAL;
                return -1;
            }
            break;
        case GDROM_DIRECT_CDDA_RESUME:
            gdrom_spi_play_resume(&packet);
            break;
        case GDROM_DIRECT_CDDA_STOP:
            if(gdrom_spi_seek(&packet, GDROM_SPI_SEEK_STOP, 0) != 0) {
                errno = EINVAL;
                return -1;
            }
            break;
        case GDROM_DIRECT_CDDA_SCAN:
            gdrom_spi_scan(&packet, reverse, speed);
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    return packet_pio_data_in_ex(&packet, NULL, 0, timeout, observed,
                                 false, true, NULL,
                                 cancel, cancel_data);
}

int gdrom_direct_cdda_pause(uint32_t timeout,
                            gdrom_direct_result_t *result) {
    return cdda_simple_internal(GDROM_DIRECT_CDDA_PAUSE, false, 0,
                                timeout, result, NULL, NULL);
}

int gdrom_direct_cdda_resume(uint32_t timeout,
                             gdrom_direct_result_t *result) {
    return cdda_simple_internal(GDROM_DIRECT_CDDA_RESUME, false, 0,
                                timeout, result, NULL, NULL);
}

int gdrom_direct_cdda_stop(uint32_t timeout,
                           gdrom_direct_result_t *result) {
    return cdda_simple_internal(GDROM_DIRECT_CDDA_STOP, false, 0,
                                timeout, result, NULL, NULL);
}

int gdrom_direct_cdda_scan(bool reverse, uint8_t speed, uint32_t timeout,
                           gdrom_direct_result_t *result) {
    return cdda_simple_internal(GDROM_DIRECT_CDDA_SCAN, reverse, speed,
                                timeout, result, NULL, NULL);
}

int gdrom_direct_get_session(uint8_t session,
                             gdrom_direct_session_t *info,
                             uint32_t timeout,
                             gdrom_direct_result_t *result) {
    gdrom_spi_packet_t packet;
    gdrom_spi_session_t decoded;
    uint8_t response[GDROM_SPI_SESSION_SIZE];
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;

    if(!info || !timeout || session > 99u) {
        errno = EINVAL;
        return -1;
    }
    if(gdrom_spi_req_session(&packet, session, sizeof(response)) != 0) {
        errno = EINVAL;
        return -1;
    }

    if(packet_pio_data_in(&packet, response, sizeof(response), timeout,
                          observed) < 0)
        return -1;
    if(observed->transferred != sizeof(response)
            || gdrom_spi_decode_session(response, sizeof(response), &decoded)
                != 0) {
        errno = EPROTO;
        return -1;
    }

    *info = (gdrom_direct_session_t) {
        .status = (cd_stat_t)decoded.status,
        .requested_session = session,
        .session_count = session == 0 ? decoded.number_or_track : 0,
        .first_track = session == 0 ? 0 : decoded.number_or_track,
        .fad = decoded.fad,
    };
    return 0;
}

int gdrom_direct_read_sectors(void *buffer, uint32_t fad, size_t sectors,
                              gdrom_direct_sector_type_t sector_type,
                              uint32_t timeout,
                              gdrom_direct_result_t *result) {
    gdrom_spi_packet_t packet;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;
    gdrom_spi_expected_type_t expected_type;
    size_t expected_bytes;

    if(!buffer || ((uintptr_t)buffer & 1u) || fad < 150u
            || fad > GDROM_SPI_MAX_U24 || !sectors
            || sectors > GDROM_DIRECT_PIO_MAX_SECTORS || !timeout
            || (sector_type != GDROM_DIRECT_SECTOR_MODE1
                && sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)
            || sectors - 1u > GDROM_SPI_MAX_U24 - fad) {
        errno = EINVAL;
        return -1;
    }

    expected_bytes = sectors * GDROM_DIRECT_SECTOR_SIZE;
    expected_type = sector_type == GDROM_DIRECT_SECTOR_MODE1
        ? GDROM_SPI_EXPECT_MODE1 : GDROM_SPI_EXPECT_MODE2_FORM1;
    if(gdrom_spi_read(&packet, GDROM_SPI_SELECT_DATA,
                      expected_type, GDROM_SPI_POINT_FAD,
                      fad, (uint32_t)sectors) != 0) {
        errno = EINVAL;
        return -1;
    }

    if(packet_pio_data_in(&packet, buffer, expected_bytes,
                          timeout, observed) < 0)
        return -1;

    /* Both supported data-only sector types have one exact transfer size.
       Treat an early, otherwise clean completion as a protocol failure. */
    if(observed->transferred != expected_bytes) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int wait_dma_inactive(uint64_t deadline) {
    while(g1_bus_dma_in_progress()) {
        if(timer_ms_gettime64() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        thd_pass();
    }

    return 0;
}

static bool dma_range_contains(uint32_t address, size_t size,
                               uint32_t lower, uint32_t upper) {
    return address >= lower && address < upper && size <= upper - address;
}

static bool dma_destination_alias_valid(uintptr_t address) {
    uintptr_t area = address & ~MEM_AREA_CACHE_MASK;

    if(area == MEM_AREA_P1_BASE || area == MEM_AREA_P2_BASE)
        return true;
    if(area == MEM_AREA_P0_BASE || area == MEM_AREA_P3_BASE)
        return !mmu_enabled();
    return false;
}

static gdrom_dma_destination_t dma_destination_classify(
        uintptr_t virtual_address, size_t size, bool gaps_authorized) {
    uint32_t physical = (uint32_t)(virtual_address & MEM_AREA_CACHE_MASK);
    uint32_t vram_size = hardware_sys_mode(NULL) == HW_TYPE_RETAIL
        ? 0x00800000u : 0x01000000u;

    if(!dma_destination_alias_valid(virtual_address))
        return GDROM_DMA_DESTINATION_INVALID;

    /* Keep both system-RAM images accepted by the existing transport. */
    if(dma_range_contains(physical, size, 0x0c000000u, 0x0d000000u)
            || dma_range_contains(physical, size,
                                  0x0e000000u, 0x0f000000u))
        return GDROM_DMA_DESTINATION_SYSTEM_RAM;

    if(dma_range_contains(physical, size,
                          0x04000000u, 0x04000000u + vram_size)
            || dma_range_contains(physical, size,
                                  0x05000000u,
                                  0x05000000u + vram_size))
        return GDROM_DMA_DESTINATION_VRAM;

    if(gaps_authorized
            && dma_range_contains(physical, size, GAPS_SRAM_PHYS_BASE,
                                   GAPS_SRAM_PHYS_BASE + GAPS_SRAM_SIZE))
        return GDROM_DMA_DESTINATION_GAPS_SRAM;

    return GDROM_DMA_DESTINATION_INVALID;
}

static bool dma_destination_cacheable(uintptr_t virtual_address,
                                      gdrom_dma_destination_t destination) {
    return destination == GDROM_DMA_DESTINATION_SYSTEM_RAM
        && (virtual_address & ~MEM_AREA_CACHE_MASK) != MEM_AREA_P2_BASE;
}

static uint32_t dma_protection_value(uint32_t address, size_t size) {
    uint32_t first = (address >> 20) & 0x7fu;
    uint32_t last = ((address + (uint32_t)size - 1u) >> 20) & 0x7fu;

    return GDROM_DMA_PROTECTION_KEY | (first << 8) | last;
}

static uint32_t excluded_dma_protection_value(uint32_t address) {
    uint32_t page = (address >> 20) & 0x7fu;
    uint32_t excluded_page = (page + 1u) & 0x7fu;

    /* Keep the DMA destination valid and make only the protection contract
       fail. This exercises Holly's illegal-address path without exposing an
       arbitrary-address fault-injection API. */
    return GDROM_DMA_PROTECTION_KEY
        | (excluded_page << 8) | excluded_page;
}

static int wait_direct_dma_events(gdrom_direct_dma_operation_t *operation,
                                  uint64_t deadline,
                                  gdrom_direct_cancel_t cancel,
                                  gdrom_direct_progress_t progress,
                                  void *hook_data) {
    uint32_t remaining;

    for(;;) {
        uint32_t wait_time;

        if(progress)
            progress(G1_IN32(G1_ATA_DMA_CURRENT_LEN), hook_data);
        if(cancel && cancel(hook_data)) {
            errno = ECANCELED;
            return -1;
        }
        if(operation->dma_event
                && operation->dma_code != ASIC_EVT_GD_DMA) {
            errno = EIO;
            return -1;
        }
        if(operation->command_event && operation->dma_event)
            return 0;
        if(deadline_timeout(deadline, &remaining) < 0)
            return -1;
        wait_time = remaining > GDROM_DMA_PROGRESS_POLL_MS
            ? GDROM_DMA_PROGRESS_POLL_MS : remaining;
        if(sem_wait_timed(operation->event, wait_time) < 0
                && errno != ETIMEDOUT)
            return -1;
    }
}

static int read_sectors_dma_internal(
        void *buffer, uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t sector_type, uint32_t timeout,
        gdrom_direct_result_t *result,
        gdrom_direct_dma_test_mode_t test_mode, semaphore_t *external_event,
        gdrom_direct_cancel_t cancel, gdrom_direct_progress_t progress,
        void *hook_data, bool gaps_authorized) {
    gdrom_direct_dma_operation_t operation;
    semaphore_t local_event;
    gdrom_spi_packet_t packet;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;
    gdrom_spi_expected_type_t expected_type;
    g1_bus_dma_client_t dma_client = G1_BUS_DMA_CLIENT_INVALID;
    uintptr_t buffer_address = (uintptr_t)buffer;
    uint32_t physical_address;
    uint32_t expected_bytes;
    uint64_t deadline;
    uint64_t cleanup_deadline;
    uint32_t remaining;
    uint8_t status;
    uint8_t reason;
    bool command_client = false;
    bool local_event_ready = false;
    bool dma_started = false;
    bool command_active = false;
    bool bus_faulted = false;
    bool cacheable = false;
    gdrom_dma_destination_t destination;
    int saved_errno = 0;
    int rv = -1;

    /* Callers may inspect the transport record after any failure, including
       argument validation. Make that contract structural rather than relying
       on which errno happens to reach a sense-decoding path today. */
    memset(observed, 0, sizeof(*observed));

    if(!buffer || (buffer_address & 31u) || fad < 150u
            || fad > GDROM_SPI_MAX_U24 || !sectors
            || sectors > GDROM_DIRECT_DMA_MAX_SECTORS || !timeout
            || (sector_type != GDROM_DIRECT_SECTOR_MODE1
                && sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)
            || sectors - 1u > GDROM_SPI_MAX_U24 - fad) {
        errno = EINVAL;
        return -1;
    }

    expected_bytes = (uint32_t)(sectors * GDROM_DIRECT_SECTOR_SIZE);
    physical_address = (uint32_t)(buffer_address & MEM_AREA_CACHE_MASK);
    destination = dma_destination_classify(buffer_address, expected_bytes,
                                           gaps_authorized);
    if(destination == GDROM_DMA_DESTINATION_INVALID) {
        errno = EFAULT;
        return -1;
    }

    expected_type = sector_type == GDROM_DIRECT_SECTOR_MODE1
        ? GDROM_SPI_EXPECT_MODE1 : GDROM_SPI_EXPECT_MODE2_FORM1;
    if(gdrom_spi_read(&packet, GDROM_SPI_SELECT_DATA,
                      expected_type, GDROM_SPI_POINT_FAD,
                      fad, (uint32_t)sectors) != 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&operation, 0, sizeof(operation));
    operation.dma_code = ASIC_EVT_GD_DMA;
    if(external_event) {
        operation.event = external_event;
        while(sem_trywait(operation.event) == 0) {
        }
    }
    else {
        if(sem_init(&local_event, 0) < 0)
            return -1;
        local_event_ready = true;
        operation.event = &local_event;
    }

    deadline = timer_ms_gettime64() + timeout;
    if(g1_bus_lock_timed(timeout) < 0)
        goto out;

    if(cancel && cancel(hook_data)) {
        errno = ECANCELED;
        goto out_unlock;
    }

    G1_OUT32(G1_ATA_PIO_RACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);
    G1_OUT32(G1_ATA_PIO_WACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);

    observed->phase = GDROM_DIRECT_PHASE_WAIT_IDLE;
    if(deadline_timeout(deadline, &remaining) < 0
            || g1_bus_select_device_timed(0, remaining, NULL) < 0
            || deadline_timeout(deadline, &remaining) < 0
            || g1_bus_wait_status(0, G1_ATA_SR_BSY | G1_ATA_SR_DRQ,
                                  remaining, &status) < 0)
        goto out_unlock;

    /* Suppress and acknowledge stale drive INTRQ before installing our
       handler. The ASIC latch is acknowledged separately so it cannot be
       mistaken for completion of the packet below. */
    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_OFF);
    (void)G1_IN8(G1_ATA_STATUS_REG);
    *(volatile uint32_t *)ASIC_ACK_B = 1u;

    /* Holly's 1001 timing and the drive's Set Features mode are independent.
       Program both so direct DMA does not depend on a BIOS side effect. */
    command_active = true;
    if(set_dma_mode2(deadline, observed) < 0)
        goto out_stop_dma;
    command_active = false;

    dma_client = g1_bus_dma_client_register(direct_dma_irq, &operation);
    if(dma_client == G1_BUS_DMA_CLIENT_INVALID)
        goto out_restore_ctl;
    operation.active = true;
    if(g1_bus_gd_command_client_register(direct_command_irq,
                                         &operation) < 0)
        goto out_deactivate;
    command_client = true;

    cacheable = dma_destination_cacheable(buffer_address, destination);
    if(cacheable)
        dcache_inval_range(buffer_address, expected_bytes);

    /* Program every Holly DMA parameter before enabling the channel. GDST is
       intentionally still clear here; it starts only after packet delivery. */
    G1_OUT32(G1_ATA_DMA_PROTECTION_P2,
             test_mode == GDROM_DIRECT_DMA_TEST_EXCLUDED_PROTECTION
                 ? excluded_dma_protection_value(physical_address)
                 : dma_protection_value(physical_address, expected_bytes));
    G1_OUT32(G1_ATA_DMA_RACCESS_WAIT, G1_ACCESS_WDMA_MODE2);
    G1_OUT32(G1_ATA_DMA_ADDRESS, physical_address);
    G1_OUT32(G1_ATA_DMA_LENGTH, expected_bytes);
    G1_OUT32(G1_ATA_DMA_DIRECTION, G1_DMA_TO_MEMORY);
    G1_OUT32(G1_ATA_DMA_ENABLE, 1);

    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_ON);
    G1_OUT8(G1_ATA_FEATURES, G1_ATA_FEATURE_DMA);
    G1_OUT8(G1_ATA_LBA_LOW, 0);
    G1_OUT8(G1_ATA_LBA_MID, 0);
    G1_OUT8(G1_ATA_LBA_HIGH, 0);
    G1_OUT8(G1_ATA_COMMAND_REG, G1_ATA_CMD_PACKET);
    command_active = true;
    ata_400ns_delay();

    observed->phase = GDROM_DIRECT_PHASE_WAIT_PACKET;
    if(wait_packet_ready(deadline, observed, cancel, hook_data) < 0)
        goto out_stop_dma;

    status = G1_IN8(G1_ATA_STATUS_REG);
    reason = G1_IN8(G1_ATA_IRQ_REASON);
    observed->ata_status = status;
    observed->interrupt_reason = reason;
    if(!(status & G1_ATA_SR_DRQ)
            || (reason & GDROM_REASON_MASK) != GDROM_REASON_PACKET_OUT) {
        errno = EPROTO;
        goto out_stop_dma;
    }

    /* PACKET request itself raises command INTRQ. It has just been
       acknowledged and is not the terminal command event paired with DMA. */
    rearm_command_irq(&operation);

    write_packet(&packet);
    ata_400ns_delay();

    if(cancel && cancel(hook_data)) {
        errno = ECANCELED;
        goto out_stop_dma;
    }

    observed->phase = GDROM_DIRECT_PHASE_WAIT_DMA;
    {
        irq_mask_t irq_state = irq_disable();

        if(!operation.command_event) {
            G1_OUT32(G1_ATA_DMA_STATUS, 1);
            dma_started = true;
        }
        irq_restore(irq_state);
    }

    /* An immediate terminal INTRQ before GDST could be armed means the drive
       rejected the packet without a DMA phase. It is safe to acknowledge now
       because Holly DMA never became active. */
    if(!dma_started) {
        status = G1_IN8(G1_ATA_STATUS_REG);
        reason = G1_IN8(G1_ATA_IRQ_REASON);
        if(!(status & (G1_ATA_SR_BSY | G1_ATA_SR_DRQ)))
            command_active = false;
        observed->ata_status = status;
        observed->interrupt_reason = reason;
        if(status & G1_ATA_SR_ERR)
            observed->ata_error = G1_IN8(G1_ATA_ERROR);
        errno = status & (G1_ATA_SR_ERR | G1_ATA_SR_DF) ? EIO : EPROTO;
        goto out_stop_dma;
    }

    if(test_mode == GDROM_DIRECT_DMA_TEST_ABORT_AFTER_START) {
        /* This is intentionally earlier than any wait. The normal cleanup
           path must stop Holly, settle or reset the drive, restore WDMA2, and
           keep the shared controller reusable. */
        errno = ECANCELED;
        goto out_stop_dma;
    }

    if(wait_direct_dma_events(&operation, deadline, cancel, progress,
                              hook_data) < 0)
        goto out_stop_dma;
    if(wait_dma_inactive(deadline) < 0)
        goto out_stop_dma;

    observed->dma_current_address =
        G1_IN32(G1_ATA_DMA_CURRENT_ADDR);
    observed->dma_transferred = G1_IN32(G1_ATA_DMA_CURRENT_LEN);
    if(progress)
        progress(observed->dma_transferred, hook_data);
    if(observed->dma_transferred != expected_bytes) {
        errno = EPROTO;
        goto out_stop_dma;
    }

    status = G1_IN8(G1_ATA_STATUS_REG);
    reason = G1_IN8(G1_ATA_IRQ_REASON);
    if(!(status & (G1_ATA_SR_BSY | G1_ATA_SR_DRQ)))
        command_active = false;
    observed->ata_status = status;
    observed->interrupt_reason = reason;
    if(status & G1_ATA_SR_ERR)
        observed->ata_error = G1_IN8(G1_ATA_ERROR);
    if(status & (G1_ATA_SR_BSY | G1_ATA_SR_DRQ)) {
        errno = EPROTO;
        goto out_stop_dma;
    }
    if(status & (G1_ATA_SR_ERR | G1_ATA_SR_DF)) {
        errno = EIO;
        goto out_stop_dma;
    }
    if((reason & GDROM_REASON_MASK) != GDROM_REASON_STATUS) {
        errno = EPROTO;
        goto out_stop_dma;
    }

    if(test_mode == GDROM_DIRECT_DMA_TEST_EXCLUDED_PROTECTION) {
        /* An emulator that ignores SB_GDAPRO can complete this transfer. Do
           not misreport that as a successful protection-path validation. */
        errno = ENOTSUP;
        goto out_stop_dma;
    }

    observed->phase = GDROM_DIRECT_PHASE_COMPLETE;
    observed->transferred = expected_bytes;
    rv = 0;

out_stop_dma:
    if(rv < 0)
        saved_errno = errno;
    g1_bus_dma_disable();
    cleanup_deadline = timer_ms_gettime64() + GDROM_DMA_CLEANUP_MS;
    if(wait_dma_inactive(cleanup_deadline) < 0) {
        if(!saved_errno)
            saved_errno = errno;
        if(g1_bus_dma_in_progress()) {
            /* Releasing G1 while SB_GDST remains active permits precisely the
               access-during-DMA fault this backend is designed to prevent. */
            g1_bus_mark_faulted();
            bus_faulted = true;
        }
    }

    if(!bus_faulted && command_active
            && settle_or_recover_command(&command_active, true,
                                         observed) < 0) {
        if(!saved_errno)
            saved_errno = errno;
        g1_bus_mark_faulted();
        bus_faulted = true;
    }

    observed->command_event = operation.command_event;
    observed->dma_event_seen = operation.dma_event;
    observed->dma_event = operation.dma_code;
    observed->dma_current_address = G1_IN32(G1_ATA_DMA_CURRENT_ADDR);
    observed->dma_transferred = G1_IN32(G1_ATA_DMA_CURRENT_LEN);
    if(rv < 0)
        observed->transferred = observed->dma_transferred < expected_bytes
            ? observed->dma_transferred : expected_bytes;
    if(cacheable && dma_started && !g1_bus_dma_in_progress())
        dcache_inval_range(buffer_address, expected_bytes);

    if(operation.command_masked && !bus_faulted) {
        *(volatile uint32_t *)ASIC_ACK_B = 1u;
        (void)g1_bus_gd_command_client_unmask();
        operation.command_masked = false;
    }
    operation.active = false;
    if(command_client)
        (void)g1_bus_gd_command_client_unregister();
out_deactivate:
    operation.active = false;
    if(dma_client != G1_BUS_DMA_CLIENT_INVALID)
        (void)g1_bus_dma_client_unregister(dma_client);
out_restore_ctl:
    if(!bus_faulted) {
        G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_ON);
        if(rv < 0 && saved_errno == EIO
                && (observed->ata_status & G1_ATA_SR_ERR)) {
            (void)capture_check_sense_locked(observed, NULL);
            if(g1_bus_is_faulted())
                bus_faulted = true;
        }
    }
out_unlock:
    if(!bus_faulted)
        g1_bus_unlock();
out:
    if(local_event_ready)
        sem_destroy(&local_event);
    if(rv < 0)
        errno = saved_errno ? saved_errno : errno;
    return rv;
}

int gdrom_direct_read_sectors_dma(
        void *buffer, uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t sector_type, uint32_t timeout,
        gdrom_direct_result_t *result) {
    return read_sectors_dma_internal(buffer, fad, sectors, sector_type,
                                     timeout, result,
                                     GDROM_DIRECT_DMA_TEST_NONE, NULL,
                                     NULL, NULL, NULL, false);
}

int gdrom_direct_read_sectors_dma_gaps(
        gaps_sram_lease_t lease, size_t offset, uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t sector_type, uint32_t timeout,
        gdrom_direct_result_t *result) {
    uint32_t physical_address;
    size_t bytes;
    int saved_errno;
    int rv;

    if(fad < 150u || fad > GDROM_SPI_MAX_U24 || !sectors
            || sectors > GDROM_DIRECT_DMA_MAX_SECTORS || !timeout
            || (sector_type != GDROM_DIRECT_SECTOR_MODE1
                && sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)
            || sectors - 1u > GDROM_SPI_MAX_U24 - fad) {
        errno = EINVAL;
        return -1;
    }
    bytes = sectors * GDROM_DIRECT_SECTOR_SIZE;
    if(gaps_sram_dma_claim(lease, offset, bytes,
                           GAPS_SRAM_DMA_OWNER_G1, &physical_address) < 0)
        return -1;

    rv = read_sectors_dma_internal(
        (void *)(MEM_AREA_P2_BASE + physical_address), fad, sectors,
        sector_type, timeout, result, GDROM_DIRECT_DMA_TEST_NONE, NULL,
        NULL, NULL, NULL, true);
    saved_errno = rv < 0 ? errno : 0;
    gaps_sram_dma_release(lease, GAPS_SRAM_DMA_OWNER_G1);
    if(rv < 0)
        errno = saved_errno;
    return rv;
}

static bool direct_request_cancelled(void *data) {
    return cdrom_request_cancel_requested_internal(data);
}

static void direct_request_progress(size_t bytes, void *data) {
    cdrom_request_update_direct_progress(data, bytes);
}

static int direct_request_result(int error,
                                 const gdrom_direct_result_t *result) {
    if(error == ECANCELED)
        return ERR_ABORTED;
    if(error == ETIMEDOUT)
        return ERR_TIMEOUT;
    if((error == EBUSY || error == EAGAIN))
        return ERR_BUSY;
    if(error == ENOENT)
        return ERR_ILLEGAL_REQUEST;
    if(error == EIO && result && result->sense_valid)
        return cdrom_sense_to_result(&result->sense);
    return ERR_SYS;
}

static int direct_mode_execute(cdrom_request_t *request, void *data) {
    gdrom_direct_async_mode_t *mode = data;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *result = mode->result
        ? mode->result : &local_result;
    int rv;

    if(mode->operation == GDROM_DIRECT_MODE_GET)
        rv = get_mode_internal(mode->mode, mode->timeout, result,
                               direct_request_cancelled, request);
    else
        rv = set_mode_internal(&mode->settings, mode->timeout, result,
                               direct_request_cancelled, request);

    if(rv == 0)
        return ERR_OK;
    return direct_request_result(errno, result);
}

cdrom_request_t *gdrom_direct_get_mode_async(
        gdrom_direct_mode_t *mode, uint32_t timeout,
        gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_mode_t operation = {
        .operation = GDROM_DIRECT_MODE_GET,
        .mode = mode,
        .timeout = timeout,
        .result = result,
    };

    if(!mode || !timeout) {
        errno = EINVAL;
        return NULL;
    }
    return cdrom_request_submit_executor(
        CD_CMD_REQ_MODE, &operation, sizeof(operation), 0, 0, 0, timeout,
        direct_mode_execute, NULL, NULL, callback, callback_data);
}

cdrom_request_t *gdrom_direct_set_mode_async(
        const gdrom_direct_mode_settings_t *settings, uint32_t timeout,
        gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_mode_t operation = {
        .operation = GDROM_DIRECT_MODE_SET,
        .timeout = timeout,
        .result = result,
    };

    if(!valid_mode_settings(settings) || !timeout) {
        errno = EINVAL;
        return NULL;
    }
    operation.settings = *settings;
    return cdrom_request_submit_executor(
        CD_CMD_SET_MODE, &operation, sizeof(operation), 0, 0, 0, timeout,
        direct_mode_execute, NULL, NULL, callback, callback_data);
}

static int direct_reinitialize_execute(cdrom_request_t *request, void *data) {
    gdrom_direct_async_reinit_t *reinit = data;
    const gdrom_direct_result_t *transport =
        &reinit->result->reset_transport;

    if(reinitialize_internal(reinit->result, reinit->timeout,
                             direct_request_cancelled, request) == 0)
        return ERR_OK;

    if(reinit->result->reset_transport.recovery_succeeded) {
        switch(reinit->result->probe.last_command) {
            case GDROM_DIRECT_PROBE_REQ_ERROR:
                transport = &reinit->result->probe.error_transport;
                break;
            case GDROM_DIRECT_PROBE_REQ_STAT:
                transport = &reinit->result->probe.status_transport;
                break;
            case GDROM_DIRECT_PROBE_TEST_UNIT:
                transport = &reinit->result->probe.test_unit_transport;
                break;
            case GDROM_DIRECT_PROBE_NONE:
            default:
                break;
        }
    }
    return direct_request_result(errno, transport);
}

cdrom_request_t *gdrom_direct_reinitialize_async(
        gdrom_direct_reinit_result_t *result, uint32_t timeout,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_reinit_t reinit = {
        .result = result,
        .timeout = timeout,
    };

    if(!result || !timeout) {
        errno = EINVAL;
        return NULL;
    }
    return cdrom_request_submit_executor(
        CD_CMD_INIT, &reinit, sizeof(reinit), 0, 0, 0, timeout,
        direct_reinitialize_execute, NULL, NULL, callback, callback_data);
}

static int direct_cdda_execute(cdrom_request_t *request, void *data) {
    gdrom_direct_async_cdda_t *cdda = data;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *result = cdda->result
        ? cdda->result : &local_result;
    int rv;

    switch(cdda->operation) {
        case GDROM_DIRECT_CDDA_PLAY:
            rv = cdda_play_internal(
                cdda->start, cdda->end, cdda->loops, cdda->mode,
                cdda->timeout, result, direct_request_cancelled, request);
            break;
        case GDROM_DIRECT_CDDA_STATUS:
            rv = cdda_status_internal(
                cdda->status, cdda->timeout, result,
                direct_request_cancelled, request);
            break;
        default:
            rv = cdda_simple_internal(
                cdda->operation, cdda->reverse, cdda->speed,
                cdda->timeout, result, direct_request_cancelled, request);
            break;
    }

    if(rv == 0)
        return ERR_OK;
    return direct_request_result(errno, result);
}

static cdrom_request_t *submit_direct_cdda(
        cd_cmd_code_t command, const gdrom_direct_async_cdda_t *cdda,
        cdrom_request_callback_t callback, void *callback_data) {
    return cdrom_request_submit_executor(
        command, cdda, sizeof(*cdda), 0, 0, 0, cdda->timeout,
        direct_cdda_execute, NULL, NULL, callback, callback_data);
}

cdrom_request_t *gdrom_direct_cdda_get_status_async(
        cdrom_cdda_status_t *status, uint32_t timeout,
        gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_cdda_t cdda = {
        .operation = GDROM_DIRECT_CDDA_STATUS,
        .timeout = timeout,
        .result = result,
        .status = status,
    };

    if(!status || !timeout) {
        errno = EINVAL;
        return NULL;
    }
    return submit_direct_cdda(CD_CMD_GETSCD, &cdda,
                              callback, callback_data);
}

cdrom_request_t *gdrom_direct_cdda_play_async(
        uint32_t start, uint32_t end, uint32_t loops, int mode,
        uint32_t timeout, gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_cdda_t cdda = {
        .operation = GDROM_DIRECT_CDDA_PLAY,
        .start = start,
        .end = end,
        .loops = loops,
        .mode = mode,
        .timeout = timeout,
        .result = result,
    };

    if(!valid_cdda_play_arguments(start, end, loops, mode, timeout)) {
        errno = EINVAL;
        return NULL;
    }
    return submit_direct_cdda(
        mode == CDDA_TRACKS ? CD_CMD_PLAY_TRACKS : CD_CMD_PLAY_SECTORS,
        &cdda, callback, callback_data);
}

static cdrom_request_t *cdda_simple_async(
        gdrom_direct_cdda_operation_t operation, cd_cmd_code_t command,
        bool reverse, uint8_t speed, uint32_t timeout,
        gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_cdda_t cdda = {
        .operation = operation,
        .reverse = reverse,
        .speed = speed,
        .timeout = timeout,
        .result = result,
    };

    if(!timeout) {
        errno = EINVAL;
        return NULL;
    }
    return submit_direct_cdda(command, &cdda, callback, callback_data);
}

cdrom_request_t *gdrom_direct_cdda_pause_async(
        uint32_t timeout, gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    return cdda_simple_async(
        GDROM_DIRECT_CDDA_PAUSE, CD_CMD_PAUSE, false, 0,
        timeout, result, callback, callback_data);
}

cdrom_request_t *gdrom_direct_cdda_resume_async(
        uint32_t timeout, gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    return cdda_simple_async(
        GDROM_DIRECT_CDDA_RESUME, CD_CMD_RELEASE, false, 0,
        timeout, result, callback, callback_data);
}

cdrom_request_t *gdrom_direct_cdda_stop_async(
        uint32_t timeout, gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    return cdda_simple_async(
        GDROM_DIRECT_CDDA_STOP, CD_CMD_STOP, false, 0,
        timeout, result, callback, callback_data);
}

cdrom_request_t *gdrom_direct_cdda_scan_async(
        bool reverse, uint8_t speed, uint32_t timeout,
        gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    return cdda_simple_async(
        GDROM_DIRECT_CDDA_SCAN, CD_CMD_SCAN_CD, reverse, speed,
        timeout, result, callback, callback_data);
}

int gdrom_direct_read_sectors_dma_request(
        cdrom_request_t *request, void *buffer, uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t sector_type, uint32_t timeout,
        gdrom_direct_result_t *result) {
    if(!request) {
        errno = EINVAL;
        return -1;
    }

    return read_sectors_dma_internal(
        buffer, fad, sectors, sector_type, timeout, result,
        GDROM_DIRECT_DMA_TEST_NONE, cdrom_request_event_internal(request),
        direct_request_cancelled, direct_request_progress, request, false);
}

static void direct_stream_release(gdrom_direct_stream_t *stream,
                                  gdrom_direct_result_t *result) {
    if(!stream)
        return;

    g1_bus_dma_disable();
    if(g1_bus_dma_in_progress()
            && wait_dma_inactive(timer_ms_gettime64()
                                 + GDROM_DMA_CLEANUP_MS) < 0) {
        g1_bus_mark_faulted();
        stream->bus_faulted = true;
    }

    if(!stream->bus_faulted && stream->command_active
            && settle_or_recover_command(&stream->command_active, true,
                                         result) < 0) {
        g1_bus_mark_faulted();
        stream->bus_faulted = true;
    }

    if(stream->operation.command_masked && !stream->bus_faulted) {
        *(volatile uint32_t *)ASIC_ACK_B = 1u;
        (void)g1_bus_gd_command_client_unmask();
        stream->operation.command_masked = false;
    }
    stream->operation.active = false;
    if(stream->command_client)
        (void)g1_bus_gd_command_client_unregister();
    if(stream->dma_client != G1_BUS_DMA_CLIENT_INVALID)
        (void)g1_bus_dma_client_unregister(stream->dma_client);

    if(!stream->bus_faulted) {
        G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_ON);
        g1_bus_unlock();
    }
    free(stream);
}

gdrom_direct_stream_t *gdrom_direct_stream_begin(
        cdrom_request_t *owner, semaphore_t *wake, uint32_t fad,
        size_t sectors, gdrom_direct_sector_type_t sector_type,
        uint32_t timeout, gdrom_direct_result_t *result) {
    gdrom_direct_stream_t *stream;
    gdrom_spi_packet_t packet;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;
    gdrom_spi_expected_type_t expected;
    uint64_t deadline;
    uint32_t remaining;
    uint8_t status;
    uint8_t reason;

    if(!owner || !wake || fad < 150u || fad > GDROM_SPI_MAX_U24
            || !sectors || sectors > UINT16_MAX || !timeout
            || sectors - 1u > GDROM_SPI_MAX_U24 - fad
            || (sector_type != GDROM_DIRECT_SECTOR_MODE1
                && sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)) {
        errno = EINVAL;
        return NULL;
    }

    expected = sector_type == GDROM_DIRECT_SECTOR_MODE1
        ? GDROM_SPI_EXPECT_MODE1 : GDROM_SPI_EXPECT_MODE2_FORM1;
    if(gdrom_spi_read2(&packet, GDROM_SPI_SELECT_DATA, expected,
                       GDROM_SPI_POINT_FAD, fad, (uint16_t)sectors,
                       fad + (uint32_t)sectors) != 0) {
        errno = EINVAL;
        return NULL;
    }

    stream = calloc(1, sizeof(*stream));
    if(!stream) {
        errno = ENOMEM;
        return NULL;
    }
    stream->dma_client = G1_BUS_DMA_CLIENT_INVALID;
    stream->operation.event = wake;
    stream->operation.dma_code = ASIC_EVT_GD_DMA;
    stream->total_bytes = sectors * GDROM_DIRECT_SECTOR_SIZE;
    memset(observed, 0, sizeof(*observed));
    observed->phase = GDROM_DIRECT_PHASE_WAIT_IDLE;

    deadline = timer_ms_gettime64() + timeout;
    if(g1_bus_lock_timed(timeout) < 0)
        goto fail_free;
    if(direct_request_cancelled(owner)) {
        errno = ECANCELED;
        goto fail_release;
    }

    G1_OUT32(G1_ATA_PIO_RACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);
    G1_OUT32(G1_ATA_PIO_WACCESS_WAIT, G1_ACCESS_PIO_DEFAULT);
    if(deadline_timeout(deadline, &remaining) < 0
            || g1_bus_select_device_timed(0, remaining, NULL) < 0
            || deadline_timeout(deadline, &remaining) < 0
            || g1_bus_wait_status(0, G1_ATA_SR_BSY | G1_ATA_SR_DRQ,
                                  remaining, &status) < 0)
        goto fail_release;

    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_OFF);
    (void)G1_IN8(G1_ATA_STATUS_REG);
    *(volatile uint32_t *)ASIC_ACK_B = 1u;

    stream->command_active = true;
    if(set_dma_mode2(deadline, observed) < 0)
        goto fail_release;
    stream->command_active = false;

    stream->dma_client = g1_bus_dma_client_register(
        direct_dma_irq, &stream->operation);
    if(stream->dma_client == G1_BUS_DMA_CLIENT_INVALID)
        goto fail_release;
    stream->operation.active = true;
    if(g1_bus_gd_command_client_register(
            direct_command_irq, &stream->operation) < 0)
        goto fail_release;
    stream->command_client = true;

    G1_OUT32(G1_ATA_DMA_RACCESS_WAIT, G1_ACCESS_WDMA_MODE2);
    G1_OUT8(G1_ATA_CTL, GDROM_CTL_INTERRUPTS_ON);
    G1_OUT8(G1_ATA_FEATURES, G1_ATA_FEATURE_DMA);
    G1_OUT8(G1_ATA_LBA_LOW, 0);
    G1_OUT8(G1_ATA_LBA_MID, 0);
    G1_OUT8(G1_ATA_LBA_HIGH, 0);
    G1_OUT8(G1_ATA_COMMAND_REG, G1_ATA_CMD_PACKET);
    stream->command_active = true;
    ata_400ns_delay();

    observed->phase = GDROM_DIRECT_PHASE_WAIT_PACKET;
    if(wait_packet_ready(deadline, observed,
                         direct_request_cancelled, owner) < 0)
        goto fail_release;

    status = G1_IN8(G1_ATA_STATUS_REG);
    reason = G1_IN8(G1_ATA_IRQ_REASON);
    observed->ata_status = status;
    observed->interrupt_reason = reason;
    if(!(status & G1_ATA_SR_DRQ)
            || (reason & GDROM_REASON_MASK) != GDROM_REASON_PACKET_OUT) {
        errno = EPROTO;
        goto fail_release;
    }

    rearm_command_irq(&stream->operation);
    write_packet(&packet);
    ata_400ns_delay();
    observed->phase = GDROM_DIRECT_PHASE_WAIT_DMA;
    return stream;

fail_release:
    direct_stream_release(stream, observed);
    return NULL;
fail_free:
    free(stream);
    return NULL;
}

static int direct_stream_terminal_status(
        gdrom_direct_stream_t *stream, cdrom_request_t *owner,
        cdrom_request_t *transfer, uint64_t deadline,
        gdrom_direct_result_t *result) {
    uint8_t status;
    uint8_t reason;

    for(;;) {
        status = G1_IN8(G1_ATA_ALTSTATUS);
        /* BSY is normally clear during a packet data phase, so it cannot by
           itself identify command completion. Wait until DRQ has also fallen
           and the interrupt-reason bits describe the terminal status phase.
           CHECK/DF are terminal regardless of the reason byte. */
        if(!(status & G1_ATA_SR_BSY)) {
            reason = G1_IN8(G1_ATA_IRQ_REASON);
            if((status & (G1_ATA_SR_ERR | G1_ATA_SR_DF))
                    || (!(status & G1_ATA_SR_DRQ)
                        && (reason & GDROM_REASON_MASK)
                            == GDROM_REASON_STATUS))
                break;
        }
        if(direct_request_cancelled(owner)
                || direct_request_cancelled(transfer)) {
            errno = ECANCELED;
            return -1;
        }
        if(timer_ms_gettime64() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        sem_wait_timed(stream->operation.event,
                       GDROM_DMA_PROGRESS_POLL_MS);
    }

    status = G1_IN8(G1_ATA_STATUS_REG);
    reason = G1_IN8(G1_ATA_IRQ_REASON);
    stream->command_active = false;
    result->phase = GDROM_DIRECT_PHASE_COMPLETE;
    result->ata_status = status;
    result->interrupt_reason = reason;
    if(status & G1_ATA_SR_ERR)
        result->ata_error = G1_IN8(G1_ATA_ERROR);
    if(status & (G1_ATA_SR_ERR | G1_ATA_SR_DF)) {
        errno = EIO;
        (void)capture_check_sense_locked(result, NULL);
        return -1;
    }
    if((reason & GDROM_REASON_MASK) != GDROM_REASON_STATUS) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int gdrom_direct_stream_transfer(
        gdrom_direct_stream_t *stream, cdrom_request_t *owner,
        cdrom_request_t *transfer, void *buffer, size_t bytes,
        uint32_t timeout, gdrom_direct_result_t *result) {
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *observed = result ? result : &local_result;
    uintptr_t buffer_address = (uintptr_t)buffer;
    uint32_t physical_address;
    uint64_t deadline;
    bool cacheable;
    gdrom_dma_destination_t destination;

    if(!stream || !owner || !transfer || !buffer || !bytes || !timeout
            || (buffer_address & 31u) || (bytes & 31u)
            || bytes > stream->total_bytes - stream->transferred_bytes) {
        errno = EINVAL;
        return -1;
    }

    physical_address = (uint32_t)(buffer_address & MEM_AREA_CACHE_MASK);
    destination = dma_destination_classify(buffer_address, bytes, false);
    /* The common stream request object currently models cacheability as a
       system-RAM alias property. Keep streams in system RAM until that layer
       can represent PVR destinations explicitly; one-shot direct DMA is the
       supported path for direct-to-PVR transfers. */
    if(destination != GDROM_DMA_DESTINATION_SYSTEM_RAM) {
        errno = EFAULT;
        return -1;
    }

    memset(observed, 0, sizeof(*observed));
    observed->phase = GDROM_DIRECT_PHASE_WAIT_DMA;
    deadline = timer_ms_gettime64() + timeout;
    cacheable = dma_destination_cacheable(buffer_address, destination);
    if(cacheable)
        dcache_inval_range(buffer_address, bytes);

    stream->operation.dma_event = false;
    stream->operation.dma_code = ASIC_EVT_GD_DMA;
    /* A streaming SPI command is already holding data in the drive buffer.
       Each transfer only re-arms Holly: protection and geometry first, enable
       second, and GDST last. */
    G1_OUT32(G1_ATA_DMA_PROTECTION_P2,
             dma_protection_value(physical_address, bytes));
    G1_OUT32(G1_ATA_DMA_ADDRESS, physical_address);
    G1_OUT32(G1_ATA_DMA_LENGTH, bytes);
    G1_OUT32(G1_ATA_DMA_DIRECTION, G1_DMA_TO_MEMORY);
    G1_OUT32(G1_ATA_DMA_ENABLE, 1);
    G1_OUT32(G1_ATA_DMA_STATUS, 1);

    for(;;) {
        size_t progress = G1_IN32(G1_ATA_DMA_CURRENT_LEN);

        observed->dma_current_address =
            G1_IN32(G1_ATA_DMA_CURRENT_ADDR);
        observed->dma_transferred = progress;
        direct_request_progress(progress, transfer);
        if(!g1_bus_dma_in_progress())
            break;
        if(direct_request_cancelled(owner)
                || direct_request_cancelled(transfer)) {
            errno = ECANCELED;
            goto fail_dma;
        }
        if(timer_ms_gettime64() >= deadline) {
            errno = ETIMEDOUT;
            goto fail_dma;
        }
        sem_wait_timed(stream->operation.event,
                       GDROM_DMA_PROGRESS_POLL_MS);
    }

    g1_bus_dma_disable();
    observed->dma_event_seen = stream->operation.dma_event;
    observed->dma_event = stream->operation.dma_code;
    observed->dma_current_address = G1_IN32(G1_ATA_DMA_CURRENT_ADDR);
    observed->dma_transferred = G1_IN32(G1_ATA_DMA_CURRENT_LEN);
    observed->transferred = observed->dma_transferred;
    if(cacheable)
        dcache_inval_range(buffer_address, bytes);

    if(stream->operation.dma_event
            && stream->operation.dma_code != ASIC_EVT_GD_DMA) {
        errno = EIO;
        return -1;
    }
    if(observed->dma_transferred != bytes) {
        errno = EPROTO;
        return -1;
    }

    stream->transferred_bytes += bytes;
    if(stream->transferred_bytes == stream->total_bytes) {
        if(direct_stream_terminal_status(stream, owner, transfer,
                                         deadline, observed) < 0)
            return -1;
    }
    else if(stream->operation.command_event) {
        /* CD_READ2 must remain active until the requested physical range has
           been drained. Early terminal INTRQ is a drive error/protocol fault. */
        if(direct_stream_terminal_status(stream, owner, transfer,
                                         deadline, observed) < 0)
            return -1;
        errno = EPROTO;
        return -1;
    }

    return 0;

fail_dma:
    g1_bus_dma_disable();
    if(wait_dma_inactive(timer_ms_gettime64() + GDROM_DMA_CLEANUP_MS) < 0
            && g1_bus_dma_in_progress()) {
        g1_bus_mark_faulted();
        stream->bus_faulted = true;
    }
    if(cacheable && !g1_bus_dma_in_progress())
        dcache_inval_range(buffer_address, bytes);
    return -1;
}

int gdrom_direct_stream_end(gdrom_direct_stream_t *stream,
                            gdrom_direct_result_t *result) {
    if(!stream) {
        errno = EINVAL;
        return -1;
    }

    direct_stream_release(stream, result);
    return g1_bus_is_faulted() ? -1 : 0;
}

static int direct_request_execute(cdrom_request_t *request, void *data) {
    gdrom_direct_async_read_t *read = data;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *result = read->result
        ? read->result : &local_result;

    uint32_t physical_address;
    bool pinned = false;
    int rv;

    if(read->gaps_lease != GAPS_SRAM_LEASE_INVALID) {
        size_t bytes = read->sectors * GDROM_DIRECT_SECTOR_SIZE;

        if(gaps_sram_dma_claim(read->gaps_lease, read->gaps_offset, bytes,
                               GAPS_SRAM_DMA_OWNER_G1,
                               &physical_address) < 0) {
            memset(result, 0, sizeof(*result));
            return direct_request_result(errno, result);
        }
        pinned = true;
        rv = read_sectors_dma_internal(
            (void *)(MEM_AREA_P2_BASE + physical_address), read->fad,
            read->sectors, read->sector_type, read->timeout, result,
            GDROM_DIRECT_DMA_TEST_NONE,
            cdrom_request_event_internal(request), direct_request_cancelled,
            direct_request_progress, request, true);
    }
    else {
        rv = gdrom_direct_read_sectors_dma_request(
            request, read->buffer, read->fad, read->sectors,
            read->sector_type, read->timeout, result);
    }

    if(pinned) {
        int saved_errno = rv < 0 ? errno : 0;

        gaps_sram_dma_release(read->gaps_lease, GAPS_SRAM_DMA_OWNER_G1);
        if(rv < 0)
            errno = saved_errno;
    }
    if(rv == 0)
        return ERR_OK;

    return direct_request_result(errno, result);
}

cdrom_request_t *gdrom_direct_read_sectors_dma_async(
        void *buffer, uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t sector_type, uint32_t timeout,
        gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_read_t read = {
        .buffer = buffer,
        .gaps_lease = GAPS_SRAM_LEASE_INVALID,
        .fad = fad,
        .sectors = sectors,
        .sector_type = sector_type,
        .timeout = timeout,
        .result = result,
    };
    size_t bytes;

    if(!buffer || ((uintptr_t)buffer & 31u) || fad < 150u
            || fad > GDROM_SPI_MAX_U24 || !sectors
            || sectors > GDROM_DIRECT_DMA_MAX_SECTORS || !timeout
            || (sector_type != GDROM_DIRECT_SECTOR_MODE1
                && sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)
            || sectors - 1u > GDROM_SPI_MAX_U24 - fad) {
        errno = EINVAL;
        return NULL;
    }

    bytes = sectors * GDROM_DIRECT_SECTOR_SIZE;
    return cdrom_request_submit_executor(
        CD_CMD_DMAREAD, &read, sizeof(read), bytes, bytes, bytes, timeout,
        direct_request_execute, NULL, NULL, callback, callback_data);
}

cdrom_request_t *gdrom_direct_read_sectors_dma_gaps_async(
        gaps_sram_lease_t lease, size_t offset, uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t sector_type, uint32_t timeout,
        gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_read_t read = {
        .buffer = NULL,
        .gaps_lease = lease,
        .gaps_offset = offset,
        .fad = fad,
        .sectors = sectors,
        .sector_type = sector_type,
        .timeout = timeout,
        .result = result,
    };
    gaps_sram_info_t info;
    size_t bytes;

    if(fad < 150u || fad > GDROM_SPI_MAX_U24 || !sectors
            || sectors > GDROM_DIRECT_DMA_MAX_SECTORS || !timeout
            || (sector_type != GDROM_DIRECT_SECTOR_MODE1
                && sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)
            || sectors - 1u > GDROM_SPI_MAX_U24 - fad) {
        errno = EINVAL;
        return NULL;
    }
    bytes = sectors * GDROM_DIRECT_SECTOR_SIZE;
    if(gaps_sram_get_info(lease, &info) < 0)
        return NULL;
    if(offset >= info.size || bytes > info.size - offset
            || ((info.physical_address + offset) & 31u)) {
        errno = EFAULT;
        return NULL;
    }

    return cdrom_request_submit_executor(
        CD_CMD_DMAREAD, &read, sizeof(read), bytes, bytes, bytes, timeout,
        direct_request_execute, NULL, NULL, callback, callback_data);
}

cdrom_stream_session_t *gdrom_direct_stream_session_start(
        uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t sector_type, uint32_t start_timeout,
        uint32_t idle_timeout) {
    if(sectors > SIZE_MAX / GDROM_DIRECT_SECTOR_SIZE) {
        errno = EOVERFLOW;
        return NULL;
    }

    return cdrom_stream_session_start_internal(
        fad, sectors, GDROM_DIRECT_SECTOR_SIZE,
        sectors * GDROM_DIRECT_SECTOR_SIZE, start_timeout, idle_timeout,
        CDROM_REQUEST_BACKEND_DIRECT, sector_type, NULL, NULL);
}

static int direct_seek_execute(cdrom_request_t *request, void *data) {
    gdrom_direct_async_seek_t *seek = data;
    gdrom_direct_result_t local_result;
    gdrom_direct_result_t *result = seek->result
        ? seek->result : &local_result;

    if(seek_internal(seek->fad, seek->timeout, result,
                     direct_request_cancelled, request) == 0)
        return ERR_OK;
    return direct_request_result(errno, result);
}

cdrom_request_t *gdrom_direct_seek_async_internal(
        uint32_t fad, uint32_t timeout, gdrom_direct_result_t *result,
        cdrom_request_finalizer_t finalizer, void *finalizer_data,
        cdrom_request_callback_t callback, void *callback_data) {
    gdrom_direct_async_seek_t seek = {
        .fad = fad,
        .timeout = timeout,
        .result = result,
    };

    if(fad < 150u || fad > GDROM_SPI_MAX_U24 || !timeout) {
        errno = EINVAL;
        return NULL;
    }

    return cdrom_request_submit_executor(
        CD_CMD_SEEK, &seek, sizeof(seek), 0, 0, 0, timeout,
        direct_seek_execute, finalizer, finalizer_data,
        callback, callback_data);
}

cdrom_request_t *gdrom_direct_seek_async(
        uint32_t fad, uint32_t timeout, gdrom_direct_result_t *result,
        cdrom_request_callback_t callback, void *callback_data) {
    return gdrom_direct_seek_async_internal(
        fad, timeout, result, NULL, NULL, callback, callback_data);
}

int gdrom_direct_dma_diagnose(
        void *buffer, uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t sector_type, uint32_t timeout,
        gdrom_direct_dma_diagnostic_t *diagnostic) {
    int rv;
    int failure = 0;

    if(!diagnostic) {
        errno = EINVAL;
        return -1;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));

    rv = read_sectors_dma_internal(
        buffer, fad, sectors, sector_type, timeout,
        &diagnostic->abort_transport,
        GDROM_DIRECT_DMA_TEST_ABORT_AFTER_START, NULL, NULL, NULL, NULL,
        false);
    diagnostic->abort_error = rv < 0 ? errno : 0;
    if(rv == 0 || diagnostic->abort_error != ECANCELED)
        failure = rv == 0 ? EPROTO : diagnostic->abort_error;

    if(!g1_bus_is_faulted()
            && gdrom_direct_read_sectors(
                buffer, fad, 1, sector_type, timeout,
                &diagnostic->post_abort_transport) == 0) {
        diagnostic->abort_reuse_succeeded = true;
    }
    else if(!failure) {
        failure = errno ? errno : EIO;
    }

    if(!g1_bus_is_faulted()) {
        rv = read_sectors_dma_internal(
            buffer, fad, sectors, sector_type, timeout,
            &diagnostic->protection_transport,
            GDROM_DIRECT_DMA_TEST_EXCLUDED_PROTECTION,
            NULL, NULL, NULL, NULL, false);
        diagnostic->protection_error = rv < 0 ? errno : 0;
        diagnostic->protection_fault_observed = rv < 0
            && diagnostic->protection_error == EIO
            && diagnostic->protection_transport.dma_event_seen
            && (diagnostic->protection_transport.dma_event
                    == ASIC_EVT_GD_DMA_ILLADDR
                || diagnostic->protection_transport.dma_event
                    == ASIC_EVT_GD_DMA_OVERRUN);

        if(!diagnostic->protection_fault_observed && !failure)
            failure = diagnostic->protection_error
                ? diagnostic->protection_error : EPROTO;
    }
    else if(!failure) {
        failure = EIO;
    }

    /* Attempt the final read even when protection reporting is unsupported.
       This distinguishes a missing emulator feature from a wedged G1 bus. */
    if(!g1_bus_is_faulted()
            && gdrom_direct_read_sectors_dma(
                buffer, fad, sectors, sector_type, timeout,
                &diagnostic->final_transport) == 0) {
        diagnostic->final_read_succeeded = true;
    }
    else if(!failure) {
        failure = errno ? errno : EIO;
    }

    if(failure) {
        errno = failure;
        return -1;
    }

    return 0;
}
