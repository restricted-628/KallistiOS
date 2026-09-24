/* KallistiOS ##version##

   CDDA control and typed-status validation.

   Run this from a mixed-mode image whose second track is audio. The test
   checks track and sector playback, synchronous and asynchronous typed
   status, pause/resume, and the relationship between track-relative time and
   absolute FAD. It intentionally uses track 2 so swapped address fields do
   not appear correct by coincidence as they can on track 1.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/cdrom.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_AUDIO_TRACK 2
#define STATUS_TIMEOUT_MS 2000
#define POSITION_TOLERANCE_FRAMES 4

static volatile unsigned media_event_count;
static volatile bool media_removed;
static volatile bool media_inserted;

static void media_event(const cdrom_media_event_t *event, void *data) {
    (void)data;
    ++media_event_count;

    if(event->type == CDROM_MEDIA_EVENT_REMOVED)
        media_removed = true;
    else if(event->type == CDROM_MEDIA_EVENT_INSERTED)
        media_inserted = true;

    /* Media callbacks must stay bounded; the foreground test prints results. */
}

static bool wait_for_flag(volatile bool *flag, uint32_t timeout_ms) {
    uint64_t deadline = timer_ms_gettime64() + timeout_ms;

    while(!*flag && timer_ms_gettime64() < deadline)
        thd_sleep(20);
    return *flag;
}

static void request_complete(cdrom_request_t *request,
                             const cdrom_request_status_t *status,
                             void *data) {
    (void)request;
    printf("%s callback: state=%d result=%d errno=%d response=%d\n",
           (const char *)data, status->state, status->result, status->error,
           status->response);
}

static void print_cdda_status(const char *label,
                              const cdrom_cdda_status_t *status) {
    printf("%s: audio=%#x track=%u index=%u elapsed=%" PRIu32
           " (%" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ") fad=%" PRIu32
           " control=%u adr=%u\n",
           label, (unsigned)status->audio_status, (unsigned)status->track,
           (unsigned)status->index, status->track_elapsed_frames,
           status->track_minutes, status->track_seconds,
           status->track_frames, status->fad, (unsigned)status->control,
           (unsigned)status->adr);
}

static int read_status(cdrom_cdda_status_t *status) {
    uint64_t deadline = timer_ms_gettime64() + STATUS_TIMEOUT_MS;
    int result;

    /* A freshly mounted image can report one unit-attention response before
       the stable subcode result. Retrying here acknowledges that transition. */
    do {
        result = cdrom_cdda_get_status(status);
        if(result == ERR_OK)
            return 0;

        if(result != ERR_DISC_CHG && result != ERR_NOT_READY
                && result != ERR_BUSY && result != ERR_RECOVERED) {
            printf("typed CDDA status failed: result=%d errno=%d\n",
                   result, cdrom_result_to_errno(result));
            return -1;
        }
        thd_sleep(20);
    } while(timer_ms_gettime64() < deadline);

    printf("typed CDDA status failed: result=%d errno=%d\n", result, errno);
    return -1;
}

static int wait_for_audio_status(cd_sub_audio_t expected,
                                 cdrom_cdda_status_t *status) {
    uint64_t deadline = timer_ms_gettime64() + STATUS_TIMEOUT_MS;

    do {
        if(read_status(status) == 0 && status->audio_status == expected)
            return 0;
        thd_sleep(20);
    } while(timer_ms_gettime64() < deadline);

    printf("timed out waiting for audio status %#x\n", (unsigned)expected);
    return -1;
}

static uint32_t frame_distance(uint32_t a, uint32_t b) {
    return a > b ? a - b : b - a;
}

static int validate_position(const char *label,
                             const cdrom_cdda_status_t *status,
                             uint32_t track_fad) {
    uint32_t derived;

    if(status->track != TEST_AUDIO_TRACK || status->index != 1) {
        printf("%s reported unexpected track/index %u/%u\n", label,
               (unsigned)status->track, (unsigned)status->index);
        return -1;
    }

    if(status->fad < track_fad) {
        printf("%s absolute FAD precedes track start (%" PRIu32
               " < %" PRIu32 ")\n", label, status->fad, track_fad);
        return -1;
    }

    derived = status->fad - track_fad;
    if(frame_distance(derived, status->track_elapsed_frames)
            > POSITION_TOLERANCE_FRAMES) {
        printf("%s address mismatch: FAD-start=%" PRIu32
               " elapsed=%" PRIu32 "\n", label, derived,
               status->track_elapsed_frames);
        return -1;
    }

    return 0;
}

static int read_status_async(cdrom_cdda_status_t *status) {
    cdrom_request_status_t request_status;
    cdrom_request_t *request = cdrom_cdda_get_status_async(
        status, STATUS_TIMEOUT_MS, request_complete, "async CDDA status");

    if(!request) {
        perror("cdrom_cdda_get_status_async");
        return -1;
    }

    if(cdrom_request_wait(request, STATUS_TIMEOUT_MS, &request_status) < 0) {
        int saved_errno = errno;

        perror("cdrom_request_wait");
        (void)cdrom_request_cancel(request);
        if(cdrom_request_wait(request, STATUS_TIMEOUT_MS, NULL) == 0
                && cdrom_request_wait_callback(
                    request, STATUS_TIMEOUT_MS) == 0)
            (void)cdrom_request_destroy(request);
        errno = saved_errno;
        return -1;
    }

    if(cdrom_request_wait_callback(request, STATUS_TIMEOUT_MS) < 0) {
        int saved_errno = errno;

        perror("cdrom_request_wait_callback");
        (void)cdrom_request_cancel(request);
        if(cdrom_request_wait(request, STATUS_TIMEOUT_MS, NULL) == 0
                && cdrom_request_wait_callback(
                    request, STATUS_TIMEOUT_MS) == 0)
            (void)cdrom_request_destroy(request);
        errno = saved_errno;
        return -1;
    }

    if(cdrom_request_destroy(request) < 0) {
        perror("cdrom_request_destroy");
        return -1;
    }

    if(request_status.state != CDROM_REQUEST_COMPLETE) {
        printf("async CDDA status failed: state=%d result=%d errno=%d\n",
               request_status.state, request_status.result,
               request_status.error);
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    cdrom_cdda_status_t first;
    cdrom_cdda_status_t second;
    cdrom_cdda_status_t paused;
    cdrom_cdda_status_t paused_later;
    cdrom_cdda_status_t resumed;
    cdrom_cdda_status_t sector_play;
    cd_toc_t toc;
    cdrom_drive_state_t drive_state;
    uint32_t track_fad;
    int media_handler;
    int failed = 0;
    int result;

    (void)argc;
    (void)argv;

    media_handler = cdrom_media_event_handler_add(media_event, NULL);
    if(media_handler < 0) {
        perror("cdrom_media_event_handler_add");
        failed = 1;
    }

    printf("CDDA test stage: read low-density TOC\n");
    result = cdrom_read_toc(&toc, false);
    if(result != ERR_OK) {
        printf("low-density TOC read failed: result=%d\n", result);
        failed = 1;
        goto done;
    }

    printf("TOC tracks=%u-%u\n", (unsigned)TOC_TRACK(toc.first),
           (unsigned)TOC_TRACK(toc.last));
    if(TOC_TRACK(toc.first) > TEST_AUDIO_TRACK
            || TOC_TRACK(toc.last) < TEST_AUDIO_TRACK
            || TOC_CTRL(toc.entry[TEST_AUDIO_TRACK - 1]) == 4) {
        printf("track %d is not an audio track in the low-density TOC\n",
               TEST_AUDIO_TRACK);
        failed = 1;
        goto done;
    }

    track_fad = TOC_LBA(toc.entry[TEST_AUDIO_TRACK - 1]);
    printf("track %d starts at FAD=%" PRIu32 "\n", TEST_AUDIO_TRACK,
           track_fad);

    printf("CDDA test stage: play track %d\n", TEST_AUDIO_TRACK);
    result = cdrom_cdda_play(TEST_AUDIO_TRACK, TEST_AUDIO_TRACK, 0,
                             CDDA_TRACKS);
    if(result != ERR_OK
            || wait_for_audio_status(CD_SUB_AUDIO_STATUS_PLAYING, &first) < 0) {
        printf("track playback failed: result=%d\n", result);
        failed = 1;
        goto done;
    }
    print_cdda_status("track playback first", &first);
    failed |= validate_position("track playback first", &first, track_fad) < 0;

    thd_sleep(500);
    if(read_status_async(&second) < 0) {
        failed = 1;
        goto done;
    }
    print_cdda_status("track playback async", &second);
    failed |= validate_position("track playback async", &second, track_fad) < 0;
    if(second.fad <= first.fad) {
        printf("playback position did not advance (%" PRIu32
               " -> %" PRIu32 ")\n", first.fad, second.fad);
        failed = 1;
    }

    printf("CDDA test stage: pause\n");
    result = cdrom_cdda_pause();
    if(result != ERR_OK
            || wait_for_audio_status(CD_SUB_AUDIO_STATUS_PAUSED, &paused) < 0) {
        printf("pause failed: result=%d\n", result);
        failed = 1;
        goto done;
    }
    print_cdda_status("paused", &paused);
    thd_sleep(250);
    if(read_status(&paused_later) < 0) {
        failed = 1;
        goto done;
    }
    print_cdda_status("paused later", &paused_later);
    if(paused_later.audio_status != CD_SUB_AUDIO_STATUS_PAUSED
            || frame_distance(paused.fad, paused_later.fad)
                > POSITION_TOLERANCE_FRAMES) {
        printf("paused position changed unexpectedly (%" PRIu32
               " -> %" PRIu32 ")\n", paused.fad, paused_later.fad);
        failed = 1;
    }

    printf("CDDA test stage: resume\n");
    result = cdrom_cdda_resume();
    if(result != ERR_OK
            || wait_for_audio_status(CD_SUB_AUDIO_STATUS_PLAYING, &resumed) < 0) {
        printf("resume failed: result=%d\n", result);
        failed = 1;
        goto done;
    }
    thd_sleep(250);
    if(read_status(&resumed) < 0) {
        failed = 1;
        goto done;
    }
    print_cdda_status("resumed", &resumed);
    if(resumed.fad <= paused_later.fad) {
        printf("resumed position did not advance (%" PRIu32
               " -> %" PRIu32 ")\n", paused_later.fad, resumed.fad);
        failed = 1;
    }

    printf("CDDA test stage: sector playback\n");
    result = cdrom_cdda_play(track_fad + 75, track_fad + 225, 0,
                             CDDA_SECTORS);
    if(result != ERR_OK
            || wait_for_audio_status(CD_SUB_AUDIO_STATUS_PLAYING,
                                     &sector_play) < 0) {
        printf("sector playback failed: result=%d\n", result);
        failed = 1;
        goto done;
    }
    print_cdda_status("sector playback", &sector_play);
    failed |= validate_position("sector playback", &sector_play, track_fad) < 0;
    if(sector_play.fad < track_fad + 75 || sector_play.fad > track_fad + 225) {
        printf("sector playback started outside requested range\n");
        failed = 1;
    }

done:
    printf("CDDA test stage: stop\n");
    result = cdrom_spin_down();
    if(result != ERR_OK) {
        printf("CDDA/drive stop failed: result=%d\n", result);
        failed = 1;
    }

    /* This last section is intentionally interactive: open Flycast's menu and
       eject, then reinsert, the same image. A timeout reports the portion as
       skipped rather than making ordinary CDDA runs fail. */
    if(media_handler >= 0) {
        printf("Media-swap test ready: eject the disc within 60 seconds.\n");
        if(wait_for_flag(&media_removed, 60000)) {
            if(cdrom_get_cached_drive_state(&drive_state) < 0
                    || (drive_state.status != CD_STATUS_OPEN
                        && drive_state.status != CD_STATUS_NO_DISC)) {
                printf("cached state did not report the removed disc\n");
                failed = 1;
            }

            printf("Media removal passed: insert the same disc within "
                   "30 seconds.\n");
            if(wait_for_flag(&media_inserted, 30000)) {
                if(cdrom_get_cached_drive_state(&drive_state) < 0
                        || drive_state.status == CD_STATUS_OPEN
                        || drive_state.status == CD_STATUS_NO_DISC
                        || drive_state.status == CD_STATUS_FATAL) {
                    printf("cached state did not report the inserted disc\n");
                    failed = 1;
                }
                else {
                    printf("Media insertion passed.\n");
                }
            }
            else {
                printf("Media insertion was not exercised.\n");
            }
        }
        else {
            printf("Media removal was not exercised.\n");
        }
    }

    if(media_handler >= 0 && cdrom_media_event_handler_remove(media_handler) < 0) {
        perror("cdrom_media_event_handler_remove");
        failed = 1;
    }

    printf("CDDA status test %s; observed %u media events. "
           "Close or reset the console to exit.\n",
           failed ? "FAILED" : "passed", media_event_count);

    for(;;)
        thd_sleep(1000);
}
