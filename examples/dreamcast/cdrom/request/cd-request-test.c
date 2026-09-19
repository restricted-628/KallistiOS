/* KallistiOS ##version##

   Asynchronous GD-ROM request example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/cdrom.h>

#include <inttypes.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t alignas(32) sector[2048];
static semaphore_t callback_target_ready = SEM_INITIALIZER(0);
static cdrom_request_t *callback_wait_target;
static volatile unsigned media_event_count;

#define REQUEST_WAIT_MS 3000u

static void media_event(const cdrom_media_event_t *event, void *data) {
    (void)event;
    (void)data;
    ++media_event_count;
}

static void request_complete(cdrom_request_t *request,
                             const cdrom_request_status_t *status,
                             void *data) {
    const char *name = data;

    (void)request;

    printf("%s callback: state=%d result=%d errno=%d response=%d "
           "data=%zu/%zu io=%zu/%zu requested=%zu "
           "sense=%#x asc=%#x ascq=%#x raw=(%" PRId32 ", %" PRId32 ")\n",
           name, status->state, status->result, status->error,
           status->response, status->completed_bytes, status->data_bytes,
           status->io_completed_bytes, status->io_bytes,
           status->requested_bytes, status->sense.key, status->sense.asc,
           status->sense.ascq, status->detail.err1, status->detail.err2);
}

static void first_request_complete(cdrom_request_t *request,
                                   const cdrom_request_status_t *status,
                                   void *data) {
    cdrom_request_status_t waited_status;

    sem_wait(&callback_target_ready);

    if(callback_wait_target &&
       cdrom_request_wait(callback_wait_target, 2000, &waited_status) < 0)
        perror("callback waiting for another request");
    else if(callback_wait_target)
        printf("callback safely waited for state=%d\n", waited_status.state);

    request_complete(request, status, data);
}

static int wait_and_destroy(const char *name, cdrom_request_t *request,
                            cdrom_request_status_t *final_status) {
    cdrom_request_status_t status;
    int failed = 0;

    if(!request)
        return 0;

    if(cdrom_request_wait(request, REQUEST_WAIT_MS, &status) < 0) {
        perror("cdrom_request_wait");
        (void)cdrom_request_cancel(request);
        if(cdrom_request_wait(request, REQUEST_WAIT_MS, &status) < 0) {
            perror("cdrom_request cleanup wait");
            return -1;
        }
        failed = 1;
    }

    printf("%s final: state=%d result=%d errno=%d\n",
           name, status.state, status.result, status.error);

    if(final_status)
        *final_status = status;

    if(cdrom_request_wait_callback(request, REQUEST_WAIT_MS) < 0) {
        perror("cdrom_request_wait_callback");
        return -1;
    }

    if(cdrom_request_destroy(request) < 0) {
        perror("cdrom_request_destroy");
        return -1;
    }

    return failed ? -1 : 0;
}

static bool wait_for_stable_drive(cdrom_drive_state_t *state) {
    int attempt;

    for(attempt = 0; attempt < 20; ++attempt) {
        if(cdrom_get_cached_drive_state(state) == 0
                && state->status != CD_STATUS_BUSY
                && state->status != CD_STATUS_RETRY)
            return true;
        thd_sleep(100);
    }

    return false;
}

int main(int argc, char **argv) {
    cdrom_request_t *first = NULL;
    cdrom_request_t *seek = NULL;
    cdrom_request_t *cancelled = NULL;
    cdrom_request_t *cdda_request = NULL;
    cdrom_request_t *last = NULL;
    cdrom_cdda_status_t cdda_status;
    cdrom_request_status_t cdda_request_status = { 0 };
    cdrom_drive_state_t drive_state;
    bool drive_state_valid;
    int media_handler;
    int failed = 0;

    (void)argc;
    (void)argv;

    media_handler = cdrom_media_event_handler_add(media_event, NULL);
    if(media_handler < 0) {
        perror("cdrom_media_event_handler_add");
        failed = 1;
    }

    drive_state_valid = wait_for_stable_drive(&drive_state);
    if(drive_state_valid)
        printf("cached drive: status=%d type=%#x sample=%" PRIu32 "\n",
               drive_state.status, drive_state.disc_type,
               drive_state.sequence);
    else
        printf("cached drive did not become stable before the test\n");

    first = cdrom_request_submit(CD_CMD_NOP, NULL, 0, 1000,
                                 first_request_complete, "first");
    seek = cdrom_seek_async(150, 1000, request_complete, "pickup seek");
    cancelled = cdrom_request_submit(CD_CMD_NOP, NULL, 0, 1000,
                                     request_complete, "cancelled");
    cdda_request = cdrom_cdda_get_status_async(
        &cdda_status, 1000, request_complete, "CDDA status");
    /* This is a real asynchronous GD DMA read, not a blocking fs_read() moved
       into the worker. With no disc inserted, an error completion is expected
       and still exercises the DMA request lifecycle safely. */
    last = cdrom_read_sectors_async(sector, 150, 1, 1000,
                                    request_complete, "DMA read");

    callback_wait_target = last;
    sem_signal(&callback_target_ready);

    if(!first || !seek || !cancelled || !cdda_request || !last) {
        perror("cdrom_request_submit");
        (void)wait_and_destroy("first cleanup", first, NULL);
        (void)wait_and_destroy("seek cleanup", seek, NULL);
        (void)wait_and_destroy("cancel cleanup", cancelled, NULL);
        (void)wait_and_destroy("CDDA cleanup", cdda_request, NULL);
        (void)wait_and_destroy("DMA cleanup", last, NULL);
        if(media_handler >= 0)
            (void)cdrom_media_event_handler_remove(media_handler);
        return 1;
    }

    /* This request is normally still queued behind the first one. */
    cdrom_request_cancel(cancelled);

    failed |= wait_and_destroy("first", first, NULL) < 0;
    failed |= wait_and_destroy("pickup seek", seek, NULL) < 0;
    failed |= wait_and_destroy("cancelled", cancelled, NULL) < 0;
    if(wait_and_destroy("CDDA status", cdda_request,
                        &cdda_request_status) < 0) {
        failed = 1;
    }
    else if(drive_state_valid && drive_state.status == CD_STATUS_NO_DISC
            && cdda_request_status.result == ERR_DISC_CHG) {
        printf("acknowledging initial unit attention and retrying CDDA status\n");
        cdda_request = cdrom_cdda_get_status_async(
            &cdda_status, 1000, request_complete, "CDDA status retry");
        if(!cdda_request || wait_and_destroy(
                "CDDA status retry", cdda_request,
                &cdda_request_status) < 0)
            failed = 1;
    }

    if(cdda_request_status.state == CDROM_REQUEST_COMPLETE) {
        printf("CDDA: audio=%#x track=%u index=%u elapsed=%" PRIu32
               ":%02" PRIu32 ":%02" PRIu32 " "
               "fad=%" PRIu32 " control=%u adr=%u\n",
               (unsigned)cdda_status.audio_status,
               (unsigned)cdda_status.track, (unsigned)cdda_status.index,
               cdda_status.track_minutes,
               cdda_status.track_seconds, cdda_status.track_frames,
               cdda_status.fad, (unsigned)cdda_status.control,
               (unsigned)cdda_status.adr);
    }
    if(drive_state_valid && drive_state.status == CD_STATUS_NO_DISC
            && (cdda_request_status.state != CDROM_REQUEST_ERROR
                || cdda_request_status.result != ERR_NO_DISC
                || cdda_request_status.sense.key != CDROM_SENSE_NOT_READY
                || cdda_request_status.sense.asc != 0x3a)) {
        printf("no-disc CDDA status did not report NOT READY / ASC 0x3a\n");
        failed = 1;
    }
    failed |= wait_and_destroy("DMA read", last, NULL) < 0;

    if(media_handler >= 0
            && cdrom_media_event_handler_remove(media_handler) < 0) {
        perror("cdrom_media_event_handler_remove");
        failed = 1;
    }

    printf("Request test %s; media events=%u. Close or reset the console "
           "to exit.\n", failed ? "FAILED" : "passed", media_event_count);

    for(;;)
        thd_sleep(1000);
}
