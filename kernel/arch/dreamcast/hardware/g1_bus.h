/* KallistiOS ##version##

   hardware/g1_bus.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __KOS_INTERNAL_G1_BUS_H
#define __KOS_INTERNAL_G1_BUS_H

#include <stdbool.h>
#include <stdint.h>

/*
   Private register vocabulary shared by the ATA slave and direct GD-ROM
   transports. These are P2 addresses: drive data is 16-bit, drive task-file
   registers are 8-bit, and Holly G1 registers are 32-bit.
*/

/* ATA task-file registers. Some addresses have different read/write roles. */
#define G1_ATA_ALTSTATUS        0xA05F7018u     /* Read */
#define G1_ATA_CTL              0xA05F7018u     /* Write */
#define G1_ATA_DATA             0xA05F7080u     /* Read/Write, 16-bit */
#define G1_ATA_ERROR            0xA05F7084u     /* Read */
#define G1_ATA_FEATURES         0xA05F7084u     /* Write */
#define G1_ATA_IRQ_REASON       0xA05F7088u     /* Read */
#define G1_ATA_SECTOR_COUNT     0xA05F7088u     /* Write */
#define G1_ATA_LBA_LOW          0xA05F708Cu     /* Read/Write */
#define G1_ATA_LBA_MID          0xA05F7090u     /* Read/Write */
#define G1_ATA_LBA_HIGH         0xA05F7094u     /* Read/Write */
#define G1_ATA_CHS_SECTOR       G1_ATA_LBA_LOW
#define G1_ATA_CHS_CYL_LOW      G1_ATA_LBA_MID
#define G1_ATA_CHS_CYL_HIGH     G1_ATA_LBA_HIGH
#define G1_ATA_DEVICE_SELECT    0xA05F7098u     /* Read/Write */
#define G1_ATA_STATUS_REG       0xA05F709Cu     /* Read, acknowledges IRQ */
#define G1_ATA_COMMAND_REG      0xA05F709Cu     /* Write */

/* Holly PIO and DMA control registers. */
#define G1_ATA_DMA_ADDRESS      0xA05F7404u     /* Read/Write */
#define G1_ATA_DMA_LENGTH       0xA05F7408u     /* Read/Write */
#define G1_ATA_DMA_DIRECTION    0xA05F740Cu     /* Read/Write */
#define G1_ATA_DMA_ENABLE       0xA05F7414u     /* Read/Write */
#define G1_ATA_DMA_STATUS       0xA05F7418u     /* Read/Write */
#define G1_ATA_PIO_RACCESS_WAIT 0xA05F7490u     /* Write-only */
#define G1_ATA_PIO_WACCESS_WAIT 0xA05F7494u     /* Write-only */
#define G1_ATA_DMA_RACCESS_WAIT 0xA05F74A0u     /* Write-only */
#define G1_ATA_DMA_WACCESS_WAIT 0xA05F74A4u     /* Write-only */
#define G1_ATA_DMA_PROTECTION_P2 0xA05F74B8u    /* Write-only */
#define G1_ATA_DMA_CURRENT_ADDR 0xA05F74F4u     /* Read-only */
#define G1_ATA_DMA_CURRENT_LEN  0xA05F74F8u     /* Read-only */

/* Status and alternate-status bits. */
#define G1_ATA_SR_ERR           0x01u
#define G1_ATA_SR_IDX           0x02u
#define G1_ATA_SR_CORR          0x04u
#define G1_ATA_SR_DRQ           0x08u
#define G1_ATA_SR_DSC           0x10u
#define G1_ATA_SR_DF            0x20u
#define G1_ATA_SR_DRDY          0x40u
#define G1_ATA_SR_BSY           0x80u

#define G1_ATA_DEVICE_SLAVE_BIT 0x10u

/* Interrupt-reason bits returned while a packet command is active. */
#define G1_ATA_IR_COD           0x01u
#define G1_ATA_IR_IO            0x02u

/* ATA commands used to establish the GD-ROM packet transport. */
#define G1_ATA_CMD_SOFT_RESET   0x08u
#define G1_ATA_CMD_PACKET       0xA0u
#define G1_ATA_CMD_IDENTIFY     0xA1u
#define G1_ATA_CMD_SET_FEATURES 0xEFu

/* Features register and host transfer-mode values. */
#define G1_ATA_FEATURE_DMA       0x01u
#define G1_ATA_FEATURE_XFER_MODE 0x03u
#define G1_ATA_XFER_PIO_DEFAULT  0x00u
#define G1_ATA_XFER_PIO_FLOW(n)  (0x08u | ((n) & 0x07u))
#define G1_ATA_XFER_WDMA(n)      (0x20u | ((n) & 0x07u))

/* Known-good Holly timings used by the existing KOS G1 ATA driver. */
#define G1_ACCESS_WDMA_MODE2    0x00001001u
#define G1_ACCESS_PIO_DEFAULT   0x00000222u

#define G1_DMA_TO_DEVICE        0u
#define G1_DMA_TO_MEMORY        1u

int g1_bus_lock(void);
int g1_bus_lock_timed(uint32_t timeout);
int g1_bus_trylock(void);
int g1_bus_unlock(void);

/* A transport that cannot stop Holly DMA must fail the shared controller
   closed. Marking it faulted releases current waiters, but every subsequent
   ownership attempt returns EIO until reboot rather than touching active G1. */
void g1_bus_mark_faulted(void);
bool g1_bus_is_faulted(void);

int g1_bus_dma_in_progress(void);

/* Disable the shared Holly G1 DMA engine. Error handlers call this before
   publishing a terminal result; SB_GDST may remain active briefly while the
   current bus unit finishes. */
void g1_bus_dma_disable(void);

/*
   G1 has one set of Holly DMA events shared by the GD-ROM master and ATA
   slave. Clients register a small IRQ-context predicate with the controller;
   the first client that reports the event handled owns that completion.
   This avoids stacking public ASIC handlers whose teardown order otherwise
   determines whether the other device still receives interrupts.
*/
typedef bool (*g1_bus_dma_irq_handler_t)(uint32_t code, void *data);
typedef int g1_bus_dma_client_t;

#define G1_BUS_DMA_CLIENT_INVALID (-1)

g1_bus_dma_client_t g1_bus_dma_client_register(
    g1_bus_dma_irq_handler_t handler, void *data);
int g1_bus_dma_client_unregister(g1_bus_dma_client_t client);

/* One direct packet transport may temporarily own the drive's command-INTRQ
   event. Registration refuses a source already owned outside this arbiter.
   The owner may mask a level-backed INTRQ in its handler and unmask it after
   thread-context code has safely acknowledged the drive status register. All
   operations are IRQ-safe. */
int g1_bus_gd_command_client_register(g1_bus_dma_irq_handler_t handler,
                                      void *data);
int g1_bus_gd_command_client_mask(void);
int g1_bus_gd_command_client_unmask(void);
int g1_bus_gd_command_client_unregister(void);

uint8_t g1_bus_device_state_init(void);
uint8_t g1_bus_select_device(uint8_t device);
int g1_bus_select_device_timed(uint8_t device, uint32_t timeout,
                               uint8_t *previous);

/*
   Wait for all bits in set_bits to become set and all bits in clear_bits to
   become clear in Alternate Status. A zero timeout preserves the historical
   unbounded ATA wait; direct GD-ROM operations always provide a deadline.
*/
int g1_bus_wait_status(uint8_t set_bits, uint8_t clear_bits,
                       uint32_t timeout, uint8_t *status);

#endif /* __KOS_INTERNAL_G1_BUS_H */
