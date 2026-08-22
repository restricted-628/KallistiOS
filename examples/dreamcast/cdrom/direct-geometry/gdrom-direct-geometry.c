/* KallistiOS ##version##

   Direct GET_TOC and REQ_SES validation.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>
#include <kos/init.h>
#include <kos/thread.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

int main(int argc, char **argv) {
    gdrom_direct_probe_result_t probe;
    gdrom_direct_result_t toc_transport = { 0 };
    gdrom_direct_result_t session_transport = { 0 };
    gdrom_direct_session_t session = { 0 };
    cd_toc_t direct_toc;
    cd_toc_t bios_toc;
    uint32_t direct_fad = 0;
    uint32_t bios_fad = 0;
    int direct_error = 0;
    int bios_result = ERR_SYS;
    bool high_density = false;
    bool passed;

    (void)argc;
    (void)argv;

    memset(&direct_toc, 0, sizeof(direct_toc));
    memset(&bios_toc, 0, sizeof(bios_toc));

    puts("Direct GD-ROM disc-geometry validation");
    if(gdrom_direct_probe(&probe, 4000) < 0
            || (probe.result != ERR_OK && probe.result != ERR_DISC_CHG)) {
        direct_error = errno ? errno : cdrom_result_to_errno(probe.result);
        printf("probe failed: result=%d errno=%d\n",
               probe.result, direct_error);
        goto done;
    }

    high_density = probe.status.disc_type == CD_GDROM;
    if(gdrom_direct_read_toc(&direct_toc, high_density, 4000,
                             &toc_transport) < 0) {
        direct_error = errno;
        printf("direct TOC failed: %s (%d)\n",
               strerror(direct_error), direct_error);
        goto done;
    }
    direct_fad = cdrom_locate_data_track(&direct_toc);

    if(gdrom_direct_get_session(0, &session, 4000,
                                &session_transport) < 0) {
        direct_error = errno;
        printf("direct session failed: %s (%d)\n",
               strerror(direct_error), direct_error);
        goto done;
    }

    bios_result = cdrom_read_toc(&bios_toc, high_density);
    if(bios_result == ERR_OK)
        bios_fad = cdrom_locate_data_track(&bios_toc);

done:
    passed = direct_error == 0
        && toc_transport.phase == GDROM_DIRECT_PHASE_COMPLETE
        && toc_transport.transferred == sizeof(cd_toc_t)
        && session_transport.phase == GDROM_DIRECT_PHASE_COMPLETE
        && session_transport.transferred == 6
        && session.requested_session == 0
        && session.session_count >= 1 && session.session_count <= 99
        && session.first_track == 0
        && session.fad == TOC_LBA(direct_toc.leadout_sector)
        && direct_fad != 0 && bios_result == ERR_OK
        && direct_fad == bios_fad
        && memcmp(&direct_toc, &bios_toc, sizeof(direct_toc)) == 0;

    printf("%s: direct_errno=%d bios=%d area=%s toc=%lu session=%lu\n",
           passed ? "PASS" : "FAIL", direct_error, bios_result,
           high_density ? "high" : "low",
           (unsigned long)toc_transport.transferred,
           (unsigned long)session_transport.transferred);
    printf("tracks=%lu-%lu sessions=%u status=%d\n",
           (unsigned long)TOC_TRACK(direct_toc.first),
           (unsigned long)TOC_TRACK(direct_toc.last),
           session.session_count, session.status);
    printf("data=%lu/%lu leadout=%lu/%lu toc_match=%d\n",
           (unsigned long)direct_fad, (unsigned long)bios_fad,
           (unsigned long)TOC_LBA(direct_toc.leadout_sector),
           (unsigned long)session.fad,
           memcmp(&direct_toc, &bios_toc, sizeof(direct_toc)) == 0);

    for(;;)
        thd_sleep(1000);
}
