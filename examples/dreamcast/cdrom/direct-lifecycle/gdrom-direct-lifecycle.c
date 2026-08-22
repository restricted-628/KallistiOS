/* KallistiOS ##version##

   Direct SPI mode and post-boot reinitialization validation.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define COMMAND_TIMEOUT_MS 4000u

KOS_INIT_FLAGS(INIT_DEFAULT);

static bool same_settings(const gdrom_direct_mode_settings_t *left,
                          const gdrom_direct_mode_settings_t *right) {
    return left->speed == right->speed
        && left->standby_seconds == right->standby_seconds
        && left->read_continuous == right->read_continuous
        && left->ecc_retry == right->ecc_retry
        && left->read_retry == right->read_retry
        && left->form2_retry == right->form2_retry
        && left->read_retry_count == right->read_retry_count;
}

static void print_mode(const char *label, const gdrom_direct_mode_t *mode) {
    printf("%s: drive='%s' version='%s' date='%s' speed=%u standby=%u "
           "flags=%u%u%u%u retries=%u\n",
           label, mode->drive_information, mode->system_version,
           mode->system_date, (unsigned)mode->settings.speed,
           (unsigned)mode->settings.standby_seconds,
           mode->settings.read_continuous, mode->settings.ecc_retry,
           mode->settings.read_retry, mode->settings.form2_retry,
           (unsigned)mode->settings.read_retry_count);
}

int main(int argc, char **argv) {
    gdrom_direct_mode_t initial_mode;
    gdrom_direct_mode_t verified_mode;
    gdrom_direct_mode_t reset_mode;
    gdrom_direct_reinit_result_t reinit;
    gdrom_direct_result_t transport;
    cd_toc_t toc;
    bool set_mode_emulator_gap = false;
    int failed = 0;

    (void)argc;
    (void)argv;

    puts("direct lifecycle: REQ_MODE");
    if(gdrom_direct_get_mode(
            &initial_mode, COMMAND_TIMEOUT_MS, &transport) < 0) {
        perror("gdrom_direct_get_mode");
        failed = 1;
        goto done;
    }
    print_mode("initial mode", &initial_mode);

    puts("direct lifecycle: no-change SET_MODE");
    if(gdrom_direct_set_mode(
            &initial_mode.settings, COMMAND_TIMEOUT_MS, &transport) < 0) {
        printf("SET_MODE trace: phase=%d status=%02x error=%02x "
               "reason=%02x count=%u transferred=%lu sense=%u/%02x/%02x\n",
               transport.phase, transport.ata_status, transport.ata_error,
               transport.interrupt_reason, transport.device_byte_count,
               (unsigned long)transport.transferred,
               transport.sense_valid ? (unsigned)transport.sense.key : 0u,
               transport.sense_valid ? transport.sense.asc : 0u,
               transport.sense_valid ? transport.sense.ascq : 0u);
        if(transport.phase == GDROM_DIRECT_PHASE_DATA_OUT
                && transport.device_byte_count == 8u
                && transport.transferred == 0u
                && transport.recovery_attempted
                && transport.recovery_succeeded
                && !transport.sense_valid) {
            puts("Flycast did not enter the expected SET_MODE data-out "
                 "phase; continuing after bounded recovery");
            set_mode_emulator_gap = true;
        }
        else {
            failed = 1;
            goto done;
        }
    }
    if(gdrom_direct_get_mode(&verified_mode, COMMAND_TIMEOUT_MS,
                             &transport) < 0) {
        perror("gdrom_direct_get_mode after set");
        failed = 1;
        goto done;
    }
    print_mode("verified mode", &verified_mode);
    if(!set_mode_emulator_gap
            && !same_settings(&initial_mode.settings,
                              &verified_mode.settings)) {
        puts("SET_MODE did not preserve the requested settings");
        failed = 1;
        goto done;
    }

    puts("direct lifecycle: post-boot reinitialize");
    if(gdrom_direct_reinitialize(&reinit, COMMAND_TIMEOUT_MS) < 0) {
        perror("gdrom_direct_reinitialize");
        failed = 1;
        goto done;
    }
    printf("reinitialize: reset_ok=%u probe_result=%d drive=%d disc=%02x\n",
           reinit.reset_transport.recovery_succeeded,
           reinit.probe.result, reinit.probe.status.status,
           reinit.probe.status.disc_type);

    if(gdrom_direct_get_mode(&reset_mode, COMMAND_TIMEOUT_MS,
                             &transport) < 0) {
        perror("gdrom_direct_get_mode after reset");
        failed = 1;
        goto done;
    }
    print_mode("reset mode", &reset_mode);

    if(cdrom_read_toc(&toc, false) != ERR_OK) {
        puts("BIOS TOC failed after direct reinitialization");
        failed = 1;
    }
    else {
        printf("BIOS reuse: tracks=%u-%u\n",
               (unsigned)TOC_TRACK(toc.first),
               (unsigned)TOC_TRACK(toc.last));
    }

done:
    if(failed)
        puts("RESULT: FAIL");
    else if(set_mode_emulator_gap)
        puts("RESULT: PARTIAL (Flycast SET_MODE phase not exercised)");
    else
        puts("RESULT: PASS");
    puts("Close or reset the console to exit.");
    for(;;)
        thd_sleep(1000);
}
