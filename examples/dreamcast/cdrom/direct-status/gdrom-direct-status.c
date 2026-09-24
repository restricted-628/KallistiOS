/* KallistiOS ##version##

   Direct GD-ROM PIO status diagnostic.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static void print_trace(const gdrom_direct_result_t *result) {
    printf("phase=%d ata_status=%02x ata_error=%02x reason=%02x "
           "byte_count=%u transferred=%lu\n",
           result->phase, result->ata_status, result->ata_error,
           result->interrupt_reason, result->device_byte_count,
           (unsigned long)result->transferred);
}

static void draw_trace(uint32_t y, const gdrom_direct_result_t *result) {
    bfont_draw_str_vram_fmt(8, y, true,
                            "phase=%d st=%02x err=%02x",
                            result->phase, result->ata_status,
                            result->ata_error);
    bfont_draw_str_vram_fmt(8, y + 24, true,
                            "reason=%02x bytes=%u/%lu",
                            result->interrupt_reason,
                            result->device_byte_count,
                            (unsigned long)result->transferred);
}

static void fill_rect(unsigned int x, unsigned int y,
                      unsigned int width, unsigned int height) {
    unsigned int row;
    unsigned int column;

    for(row = 0; row < height; ++row)
        for(column = 0; column < width; ++column)
            vram_s[(y + row) * 320 + x + column] = 0xffff;
}

static void draw_hex_digit(unsigned int x, unsigned int y, uint8_t digit) {
    static const uint8_t segments[16] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
        0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71
    };
    uint8_t mask = segments[digit & 0x0f];

    if(mask & 0x01) fill_rect(x + 4, y, 16, 4);
    if(mask & 0x02) fill_rect(x + 20, y + 4, 4, 14);
    if(mask & 0x04) fill_rect(x + 20, y + 22, 4, 14);
    if(mask & 0x08) fill_rect(x + 4, y + 36, 16, 4);
    if(mask & 0x10) fill_rect(x, y + 22, 4, 14);
    if(mask & 0x20) fill_rect(x, y + 4, 4, 14);
    if(mask & 0x40) fill_rect(x + 4, y + 18, 16, 4);
}

static void draw_hex_byte(unsigned int x, unsigned int y, uint8_t value) {
    draw_hex_digit(x, y, value >> 4);
    draw_hex_digit(x + 30, y, value);
}

static const gdrom_direct_result_t *last_transport(
        const gdrom_direct_probe_result_t *probe) {
    switch(probe->last_command) {
        case GDROM_DIRECT_PROBE_REQ_ERROR:
            return &probe->error_transport;
        case GDROM_DIRECT_PROBE_REQ_STAT:
            return &probe->status_transport;
        case GDROM_DIRECT_PROBE_TEST_UNIT:
        case GDROM_DIRECT_PROBE_NONE:
        default:
            return &probe->test_unit_transport;
    }
}

int main(int argc, char **argv) {
    gdrom_direct_probe_result_t probe;

    (void)argc;
    (void)argv;

    vid_set_mode(DM_320x240, PM_RGB565);
    memset(vram_s, 0, 320 * 240 * sizeof(uint16_t));
    bfont_draw_str_vram_fmt(8, 8, true, "Direct GD-ROM SPI probe");
    bfont_draw_str_vram_fmt(8, 32, true, "running TEST/ERROR/STAT");

    puts("Direct GD-ROM SPI readiness probe");
    if(gdrom_direct_probe(&probe, 4000) < 0) {
        int saved_errno = errno;
        const gdrom_direct_result_t *transport = last_transport(&probe);

        printf("probe transport failed: %s\n", strerror(saved_errno));
        print_trace(transport);
        bfont_draw_str_vram_fmt(8, 72, true,
                                "FAILED: %s (%d)",
                                strerror(saved_errno), saved_errno);
        bfont_draw_str_vram_fmt(8, 96, true,
                                "cmd=%d stat=%u err=%u",
                                probe.last_command, probe.status_requests,
                                probe.error_requests);
        draw_trace(120, transport);
    }
    else {
        printf("probe result=%d status_requests=%u error_requests=%u\n",
               probe.result, probe.status_requests, probe.error_requests);
        printf("drive=%d disc=%02x track=%u index=%u fad=%lu\n",
               probe.status.status, probe.status.disc_type,
               probe.status.track, probe.status.index,
               (unsigned long)probe.status.fad);
        if(probe.error_valid)
            printf("sense=%x asc=%02x ascq=%02x info=%08lx\n",
                   probe.error.sense.key, probe.error.sense.asc,
                   probe.error.sense.ascq,
                   (unsigned long)probe.error.command_specific_information);
        print_trace(&probe.status_transport);

        bfont_draw_str_vram_fmt(8, 72, true,
                                "PROBE OK result=%d", probe.result);
        bfont_draw_str_vram_fmt(8, 96, true,
                                "drive=%d disc=%02x",
                                probe.status.status, probe.status.disc_type);
        bfont_draw_str_vram_fmt(8, 120, true,
                                "requests stat=%u err=%u",
                                probe.status_requests, probe.error_requests);
        bfont_draw_str_vram_fmt(8, 144, true, "KEY     ASC    ASCQ");
        if(probe.error_valid) {
            draw_hex_byte(8, 176, probe.error.sense.key);
            draw_hex_byte(112, 176, probe.error.sense.asc);
            draw_hex_byte(224, 176, probe.error.sense.ascq);
        }
    }

    for(;;)
        thd_sleep(1000);
}
