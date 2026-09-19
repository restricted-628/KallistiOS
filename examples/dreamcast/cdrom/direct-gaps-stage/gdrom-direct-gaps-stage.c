/* KallistiOS ##version##

   Direct GD-to-GAPS staging validation.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dc/cdrom.h>
#include <dc/g2bus.h>
#include <dc/gaps.h>
#include <dc/gdrom_direct.h>
#include <kos/init.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEST_SECTORS GDROM_DIRECT_DMA_MAX_SECTORS
#define TEST_BYTES   (TEST_SECTORS * GDROM_DIRECT_SECTOR_SIZE)
#define REQUEST_TIMEOUT_MS 8000u

_Alignas(32) static uint8_t staged_buffer[TEST_BYTES];
_Alignas(32) static uint8_t reference_buffer[TEST_BYTES];

static int destroy_request(cdrom_request_t *request) {
    if(cdrom_request_wait_callback(request, REQUEST_TIMEOUT_MS) < 0)
        return -1;
    return cdrom_request_destroy(request);
}

int main(int argc, char **argv) {
    gdrom_direct_probe_result_t probe;
    gdrom_direct_result_t transport;
    gdrom_direct_sector_type_t sector_type;
    gaps_sram_lease_t lease = GAPS_SRAM_LEASE_INVALID;
    gaps_sram_info_t sram_info;
    cdrom_request_status_t request_status;
    cdrom_request_t *request = NULL;
    cd_toc_t toc;
    uint32_t fad;
    int failed = 0;

    (void)argc;
    (void)argv;

    puts("Direct GD-to-GAPS staging validation");
    if(!gaps_probe() || gaps_init() < 0) {
        puts("DIRECT-GAPS-STAGE: SKIP bridge unavailable");
        return EXIT_SUCCESS;
    }
    if(gaps_sram_alloc(TEST_BYTES, 2048, &lease) < 0) {
        puts("DIRECT-GAPS-STAGE: SKIP SRAM already owned");
        (void)gaps_shutdown();
        return EXIT_SUCCESS;
    }
    if(gaps_sram_get_info(lease, &sram_info) < 0) {
        failed = 1;
        goto done;
    }

    if(gdrom_direct_probe(&probe, 4000) < 0
            || (probe.result != ERR_OK && probe.result != ERR_DISC_CHG)) {
        perror("direct probe");
        failed = 1;
        goto done;
    }
    if(cdrom_read_toc(&toc, probe.status.disc_type == CD_GDROM) != ERR_OK) {
        puts("TOC read failed");
        failed = 1;
        goto done;
    }
    fad = cdrom_locate_data_track(&toc);
    if(!fad) {
        puts("data track not found");
        failed = 1;
        goto done;
    }
    sector_type = probe.status.disc_type == CD_CDROM_XA
            || probe.status.disc_type == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1
        : GDROM_DIRECT_SECTOR_MODE1;

    memset(&transport, 0, sizeof(transport));
    request = gdrom_direct_read_sectors_dma_gaps_async(
        lease, 0, fad, TEST_SECTORS, sector_type, 4000,
        &transport, NULL, NULL);
    if(!request
            || cdrom_request_wait(request, REQUEST_TIMEOUT_MS,
                                  &request_status) < 0
            || request_status.state != CDROM_REQUEST_COMPLETE
            || request_status.completed_bytes != TEST_BYTES) {
        perror("direct SRAM request");
        failed = 1;
        goto done;
    }
    if(destroy_request(request) < 0) {
        perror("request destroy");
        failed = 1;
        request = NULL;
        goto done;
    }
    request = NULL;

    if(g2_dma_transfer(staged_buffer,
                       (void *)(uintptr_t)sram_info.physical_address,
                       TEST_BYTES, 1, NULL, NULL, G2_DMA_TO_SH4, 0,
                       G2_DMA_CHAN_CH2, 0) < 0) {
        perror("G2 SRAM transfer");
        failed = 1;
        goto done;
    }
    if(cdrom_read_sectors(reference_buffer, fad, TEST_SECTORS) != ERR_OK
            || memcmp(staged_buffer, reference_buffer, TEST_BYTES)) {
        puts("payload comparison failed");
        failed = 1;
    }

done:
    if(request) {
        (void)cdrom_request_cancel(request);
        (void)cdrom_request_wait(request, REQUEST_TIMEOUT_MS, NULL);
        (void)destroy_request(request);
    }
    if(lease != GAPS_SRAM_LEASE_INVALID)
        (void)gaps_sram_free(lease);
    (void)gaps_shutdown();
    printf("DIRECT-GAPS-STAGE: %s\n", failed ? "FAIL" : "PASS");
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
