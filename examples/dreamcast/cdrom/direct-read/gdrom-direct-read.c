/* KallistiOS ##version##

   Direct GD-ROM BIOS/PIO/DMA sector-read validation.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/asic.h>
#include <dc/biosfont.h>
#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>
#include <dc/video.h>
#include <kos/init.h>
#include <kos/thread.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEST_SECTORS   2u
#define TEST_BYTES     (TEST_SECTORS * GDROM_DIRECT_SECTOR_SIZE)
#define GUARD_BYTES    32u
#define GUARD_VALUE    0xa5u

_Alignas(32) static uint8_t pio_buffer[TEST_BYTES + GUARD_BYTES];
_Alignas(32) static uint8_t dma_buffer[TEST_BYTES + GUARD_BYTES];
_Alignas(32) static uint8_t bios_buffer[TEST_BYTES + GUARD_BYTES];

static bool guard_intact(const uint8_t *buffer) {
    size_t i;

    for(i = TEST_BYTES; i < TEST_BYTES + GUARD_BYTES; ++i) {
        if(buffer[i] != GUARD_VALUE)
            return false;
    }
    return true;
}

static uint32_t checksum(const uint8_t *buffer, size_t size) {
    uint32_t value = 2166136261u;
    size_t i;

    for(i = 0; i < size; ++i) {
        value ^= buffer[i];
        value *= 16777619u;
    }
    return value;
}

static void fill_rect(unsigned int x, unsigned int y,
                      unsigned int width, unsigned int height,
                      uint16_t color) {
    unsigned int row;
    unsigned int column;

    for(row = 0; row < height; ++row)
        for(column = 0; column < width; ++column)
            vram_s[(y + row) * 320 + x + column] = color;
}

static void draw_hex_digit(unsigned int x, unsigned int y, uint8_t digit) {
    static const uint8_t segments[16] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
        0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71
    };
    uint8_t mask = segments[digit & 0x0f];

    if(mask & 0x01) fill_rect(x + 4, y, 16, 4, 0xffff);
    if(mask & 0x02) fill_rect(x + 20, y + 4, 4, 14, 0xffff);
    if(mask & 0x04) fill_rect(x + 20, y + 22, 4, 14, 0xffff);
    if(mask & 0x08) fill_rect(x + 4, y + 36, 16, 4, 0xffff);
    if(mask & 0x10) fill_rect(x, y + 22, 4, 14, 0xffff);
    if(mask & 0x20) fill_rect(x, y + 4, 4, 14, 0xffff);
    if(mask & 0x40) fill_rect(x + 4, y + 18, 16, 4, 0xffff);
}

static void draw_hex_byte(unsigned int x, unsigned int y, uint8_t value) {
    draw_hex_digit(x, y, value >> 4);
    draw_hex_digit(x + 30, y, value);
}

static void draw_stage(const char *stage) {
    vid_set_mode(DM_320x240, PM_RGB565);
    fill_rect(0, 0, 320, 240, 0x0000);
    bfont_draw_str_vram_fmt(8, 8, true, "Direct PIO/DMA CD_READ");
    bfont_draw_str_vram_fmt(8, 48, true, "Running:");
    bfont_draw_str_vram_fmt(8, 80, true, "%s", stage);
}

static void draw_result(bool passed, int direct_error, int bios_result,
                        const gdrom_direct_result_t *dma_transport,
                        uint32_t fad, uint32_t pio_sum,
                        uint32_t dma_sum, uint32_t bios_sum) {
    vid_set_mode(DM_320x240, PM_RGB565);
    fill_rect(0, 0, 320, 240, passed ? 0x0200 : 0x4000);
    bfont_draw_str_vram_fmt(8, 8, true, "Direct PIO/DMA CD_READ");
    bfont_draw_str_vram_fmt(8, 40, true, passed ? "PASS" : "FAIL");
    bfont_draw_str_vram_fmt(8, 72, true,
                            "fad=%lu phase=%d bytes=%lu",
                            (unsigned long)fad, dma_transport->phase,
                            (unsigned long)dma_transport->transferred);
    bfont_draw_str_vram_fmt(8, 96, true,
                            "st=%02x err=%02x rsn=%02x",
                            dma_transport->ata_status,
                            dma_transport->ata_error,
                            dma_transport->interrupt_reason);
    bfont_draw_str_vram_fmt(8, 128, true, "ERR      BIOS     PHASE");
    draw_hex_byte(8, 160, (uint8_t)direct_error);
    draw_hex_byte(112, 160, (uint8_t)bios_result);
    draw_hex_byte(224, 160, (uint8_t)dma_transport->phase);
    bfont_draw_str_vram_fmt(8, 216, true, "%08lx/%08lx/%08lx",
                            (unsigned long)pio_sum,
                            (unsigned long)dma_sum,
                            (unsigned long)bios_sum);
}

int main(int argc, char **argv) {
    gdrom_direct_probe_result_t probe;
    gdrom_direct_result_t pio_transport;
    gdrom_direct_result_t dma_transport;
    cd_toc_t toc;
    uint32_t test_fad = 0;
    gdrom_direct_sector_type_t sector_type;
    uint32_t pio_sum = 0;
    uint32_t dma_sum = 0;
    uint32_t bios_sum = 0;
    int direct_error = 0;
    int bios_result = ERR_SYS;
    bool passed = false;

    (void)argc;
    (void)argv;

    memset(pio_buffer, GUARD_VALUE, sizeof(pio_buffer));
    memset(dma_buffer, GUARD_VALUE, sizeof(dma_buffer));
    memset(bios_buffer, GUARD_VALUE, sizeof(bios_buffer));
    memset(&pio_transport, 0, sizeof(pio_transport));
    memset(&dma_transport, 0, sizeof(dma_transport));

    draw_stage("readiness probe");
    puts("Direct GD-ROM BIOS/PIO/DMA sector-read validation");
    if(gdrom_direct_probe(&probe, 4000) < 0) {
        direct_error = errno;
        printf("readiness probe failed: %s\n", strerror(direct_error));
        goto done;
    }
    /* A just-inserted image may retain the acknowledged unit-attention result
       even though the following status request reports readable media. */
    if(probe.result != ERR_OK && probe.result != ERR_DISC_CHG) {
        direct_error = cdrom_result_to_errno(probe.result);
        printf("drive is not ready: result=%d errno=%d\n",
               probe.result, direct_error);
        goto done;
    }

    draw_stage("BIOS TOC read");
    bios_result = cdrom_read_toc(&toc, probe.status.disc_type == CD_GDROM);
    if(bios_result != ERR_OK) {
        printf("TOC read failed: result=%d\n", bios_result);
        goto done;
    }
    test_fad = cdrom_locate_data_track(&toc);
    if(!test_fad) {
        direct_error = ENOENT;
        puts("no data track in TOC");
        goto done;
    }
    sector_type = probe.status.disc_type == CD_CDROM_XA
            || probe.status.disc_type == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1
        : GDROM_DIRECT_SECTOR_MODE1;

    draw_stage("direct PIO read");
    if(gdrom_direct_read_sectors(pio_buffer, test_fad, TEST_SECTORS,
                                 sector_type, 4000, &pio_transport) < 0) {
        direct_error = errno;
        printf("direct PIO read failed: %s\n", strerror(direct_error));
        goto done;
    }
    draw_stage("direct DMA read");
    if(gdrom_direct_read_sectors_dma(dma_buffer, test_fad, TEST_SECTORS,
                                     sector_type, 4000,
                                     &dma_transport) < 0) {
        direct_error = errno;
        printf("direct DMA read failed: %s\n", strerror(direct_error));
        goto done;
    }

    draw_stage("BIOS sector read");
    bios_result = cdrom_read_sectors(bios_buffer, test_fad, TEST_SECTORS);
    pio_sum = checksum(pio_buffer, TEST_BYTES);
    dma_sum = checksum(dma_buffer, TEST_BYTES);
    bios_sum = checksum(bios_buffer, TEST_BYTES);
    passed = bios_result == ERR_OK
        && pio_transport.transferred == TEST_BYTES
        && dma_transport.transferred == TEST_BYTES
        && dma_transport.command_event && dma_transport.dma_event_seen
        && dma_transport.dma_event == ASIC_EVT_GD_DMA
        && guard_intact(pio_buffer) && guard_intact(dma_buffer)
        && guard_intact(bios_buffer)
        && memcmp(pio_buffer, dma_buffer, TEST_BYTES) == 0
        && memcmp(dma_buffer, bios_buffer, TEST_BYTES) == 0;

done:
    printf("%s: direct_errno=%d bios_result=%d phase=%d bytes=%lu\n",
           passed ? "PASS" : "FAIL", direct_error, bios_result,
           dma_transport.phase, (unsigned long)dma_transport.transferred);
    printf("events: command=%d dma=%d/%04lx progress=%lu\n",
           dma_transport.command_event, dma_transport.dma_event_seen,
           (unsigned long)dma_transport.dma_event,
           (unsigned long)dma_transport.dma_transferred);
    printf("checksums: pio=%08lx dma=%08lx bios=%08lx guards=%d/%d/%d\n",
           (unsigned long)pio_sum, (unsigned long)dma_sum,
           (unsigned long)bios_sum, guard_intact(pio_buffer),
           guard_intact(dma_buffer), guard_intact(bios_buffer));
    printf("data track FAD=%lu\n", (unsigned long)test_fad);
    draw_result(passed, direct_error, bios_result, &dma_transport,
                test_fad, pio_sum, dma_sum, bios_sum);

    for(;;)
        thd_sleep(1000);
}
