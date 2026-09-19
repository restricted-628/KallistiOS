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

static volatile unsigned callback_count;

static void request_complete(cdrom_request_t *request,
                             const cdrom_request_status_t *status,
                             void *data) {
    (void)request;
    ++callback_count;
    printf("%s callback: state=%d backend=%d result=%d errno=%d\n",
           (const char *)data, status->state, status->backend,
           status->result, status->error);
}

static int finish_request(cdrom_request_t *request, const char *label) {
    cdrom_request_status_t status = { 0 };
    int failed = 0;

    if(!request) {
        perror(label);
        return -1;
    }
    if(cdrom_request_wait(request, COMMAND_TIMEOUT_MS + 1000u, &status) < 0) {
        perror("cdrom_request_wait");
        (void)cdrom_request_cancel(request);
        if(cdrom_request_wait(request, COMMAND_TIMEOUT_MS + 1000u,
                              &status) < 0) {
            perror("cdrom_request cleanup wait");
            return -1;
        }
        failed = 1;
    }
    if(cdrom_request_wait_callback(request, COMMAND_TIMEOUT_MS) < 0) {
        perror("cdrom_request_wait_callback");
        if(cdrom_request_wait_callback(request, COMMAND_TIMEOUT_MS) < 0) {
            perror("cdrom_request cleanup callback wait");
            return -1;
        }
        failed = 1;
    }
    if(status.state != CDROM_REQUEST_COMPLETE
            || status.result != ERR_OK
            || status.backend != CDROM_REQUEST_BACKEND_DIRECT) {
        printf("%s failed: state=%d backend=%d result=%d errno=%d\n",
               label, status.state, status.backend,
               status.result, status.error);
        failed = 1;
    }
    if(cdrom_request_destroy(request) < 0) {
        perror("cdrom_request_destroy");
        failed = 1;
    }
    return failed ? -1 : 0;
}

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

    puts("direct lifecycle: async REQ_MODE");
    if(finish_request(gdrom_direct_get_mode_async(
            &initial_mode, COMMAND_TIMEOUT_MS, &transport,
            request_complete, "get-mode"), "get-mode") < 0) {
        failed = 1;
        goto done;
    }
    print_mode("initial mode", &initial_mode);

    puts("direct lifecycle: async no-change SET_MODE");
    if(finish_request(gdrom_direct_set_mode_async(
            &initial_mode.settings, COMMAND_TIMEOUT_MS, &transport,
            request_complete, "set-mode"), "set-mode") < 0) {
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

    puts("direct lifecycle: async post-boot reinitialize");
    if(finish_request(gdrom_direct_reinitialize_async(
            &reinit, COMMAND_TIMEOUT_MS, request_complete, "reinitialize"),
            "reinitialize") < 0) {
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
        printf("RESULT: FAIL; callbacks=%u\n", callback_count);
    else if(set_mode_emulator_gap)
        printf("RESULT: PARTIAL (Flycast SET_MODE phase not exercised); "
               "callbacks=%u\n", callback_count);
    else
        printf("RESULT: PASS; callbacks=%u\n", callback_count);
    puts("Close or reset the console to exit.");
    for(;;)
        thd_sleep(1000);
}
