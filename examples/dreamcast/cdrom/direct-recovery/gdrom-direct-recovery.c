/* KallistiOS ##version##

   Controlled direct GD-ROM DMA abort/protection recovery validation.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/asic.h>
#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>
#include <kos/init.h>
#include <kos/thread.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEST_SECTORS GDROM_DIRECT_DMA_MAX_SECTORS
#define TEST_BYTES   (TEST_SECTORS * GDROM_DIRECT_SECTOR_SIZE)
#define GUARD_BYTES  32u
#define GUARD_VALUE  0xa5u

_Alignas(32) static uint8_t direct_buffer[TEST_BYTES + GUARD_BYTES];
_Alignas(32) static uint8_t bios_buffer[TEST_BYTES + GUARD_BYTES];

static bool guard_intact(const uint8_t *buffer) {
    size_t i;

    for(i = TEST_BYTES; i < TEST_BYTES + GUARD_BYTES; ++i) {
        if(buffer[i] != GUARD_VALUE)
            return false;
    }
    return true;
}

int main(int argc, char **argv) {
    gdrom_direct_probe_result_t probe;
    gdrom_direct_dma_diagnostic_t diagnostic;
    gdrom_direct_sector_type_t sector_type;
    cd_toc_t toc;
    uint32_t fad;
    int diagnostic_error = 0;
    int bios_result = ERR_SYS;
    bool payload_matches = false;
    bool passed;

    (void)argc;
    (void)argv;

    memset(direct_buffer, GUARD_VALUE, sizeof(direct_buffer));
    memset(bios_buffer, GUARD_VALUE, sizeof(bios_buffer));
    memset(&diagnostic, 0, sizeof(diagnostic));

    puts("Direct GD-ROM DMA recovery validation");
    if(gdrom_direct_probe(&probe, 4000) < 0
            || (probe.result != ERR_OK && probe.result != ERR_DISC_CHG)) {
        diagnostic_error = errno ? errno : cdrom_result_to_errno(probe.result);
        printf("probe failed: result=%d errno=%d\n",
               probe.result, diagnostic_error);
        goto done;
    }

    bios_result = cdrom_read_toc(&toc, probe.status.disc_type == CD_GDROM);
    if(bios_result != ERR_OK) {
        printf("TOC failed: result=%d\n", bios_result);
        goto done;
    }

    fad = cdrom_locate_data_track(&toc);
    if(!fad) {
        diagnostic_error = ENOENT;
        puts("no data track");
        goto done;
    }

    sector_type = probe.status.disc_type == CD_CDROM_XA
            || probe.status.disc_type == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1
        : GDROM_DIRECT_SECTOR_MODE1;

    if(gdrom_direct_dma_diagnose(
            direct_buffer, fad, TEST_SECTORS, sector_type, 4000,
            &diagnostic) < 0) {
        diagnostic_error = errno;
        printf("diagnostic result: %s (%d)\n",
               strerror(diagnostic_error), diagnostic_error);
    }

    bios_result = cdrom_read_sectors(bios_buffer, fad, TEST_SECTORS);
    payload_matches = bios_result == ERR_OK
        && memcmp(direct_buffer, bios_buffer, TEST_BYTES) == 0;

done:
    passed = diagnostic_error == 0
        && diagnostic.abort_error == ECANCELED
        && diagnostic.abort_reuse_succeeded
        && diagnostic.protection_fault_observed
        && (diagnostic.protection_transport.dma_event
                == ASIC_EVT_GD_DMA_ILLADDR
            || diagnostic.protection_transport.dma_event
                == ASIC_EVT_GD_DMA_OVERRUN)
        && diagnostic.final_read_succeeded
        && payload_matches
        && guard_intact(direct_buffer)
        && guard_intact(bios_buffer);

    printf("%s: abort=%d reuse=%d protection=%d/%04lx final=%d\n",
           passed ? "PASS" : "FAIL", diagnostic.abort_error,
           diagnostic.abort_reuse_succeeded,
           diagnostic.protection_fault_observed,
           (unsigned long)diagnostic.protection_transport.dma_event,
           diagnostic.final_read_succeeded);
    printf("bios=%d match=%d guards=%d/%d bus-progress=%lu\n",
           bios_result, payload_matches, guard_intact(direct_buffer),
           guard_intact(bios_buffer),
           (unsigned long)diagnostic.final_transport.dma_transferred);

    for(;;)
        thd_sleep(1000);
}
