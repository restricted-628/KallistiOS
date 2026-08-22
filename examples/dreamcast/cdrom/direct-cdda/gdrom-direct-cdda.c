/* KallistiOS ##version##

   Direct SPI CDDA and subcode validation.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COMMAND_TIMEOUT_MS 3000u
#define STATUS_DEADLINE_MS 3000u

static int read_status_until(cd_sub_audio_t expected,
                             cdrom_cdda_status_t *status) {
    uint64_t deadline = timer_ms_gettime64() + STATUS_DEADLINE_MS;

    do {
        gdrom_direct_result_t transport;

        if(gdrom_direct_cdda_get_status(
                status, COMMAND_TIMEOUT_MS, &transport) == 0
                && status->audio_status == expected)
            return 0;

        if(errno != EAGAIN && errno != EBUSY && errno != ESTALE) {
            perror("gdrom_direct_cdda_get_status");
            return -1;
        }
        thd_sleep(20);
    } while(timer_ms_gettime64() < deadline);

    printf("timed out waiting for direct audio status %#x\n",
           (unsigned)expected);
    return -1;
}

static void print_status(const char *label,
                         const cdrom_cdda_status_t *status) {
    printf("%s: audio=%#x track=%u index=%u elapsed=%" PRIu32
           " fad=%" PRIu32 " ctrl=%u adr=%u\n",
           label, (unsigned)status->audio_status,
           (unsigned)status->track, (unsigned)status->index,
           status->track_elapsed_frames, status->fad,
           (unsigned)status->control, (unsigned)status->adr);
}

int main(int argc, char **argv) {
    gdrom_direct_result_t transport;
    cdrom_cdda_status_t status;
    cd_toc_t direct_toc;
    cd_toc_t bios_toc;
    uint8_t raw_q[14];
    uint32_t first;
    uint32_t last;
    uint32_t track = 0;
    uint32_t track_fad;
    uint32_t track_end;
    uint32_t i;
    int failed = 0;

    (void)argc;
    (void)argv;

    printf("direct CDDA: read low-density TOC\n");
    if(gdrom_direct_read_toc(&direct_toc, false,
                             COMMAND_TIMEOUT_MS, &transport) < 0) {
        perror("gdrom_direct_read_toc");
        failed = 1;
        goto done;
    }

    first = TOC_TRACK(direct_toc.first);
    last = TOC_TRACK(direct_toc.last);
    for(i = first; i <= last && i <= 99u; ++i) {
        if(direct_toc.entry[i - 1u] != 0xffffffffu
                && TOC_CTRL(direct_toc.entry[i - 1u]) != 4u) {
            track = i;
            break;
        }
    }
    if(!track) {
        printf("no low-density audio track found in TOC %" PRIu32
               "-%" PRIu32 "\n", first, last);
        failed = 1;
        goto done;
    }

    track_fad = TOC_LBA(direct_toc.entry[track - 1u]);
    track_end = track < last ? TOC_LBA(direct_toc.entry[track])
                             : TOC_LBA(direct_toc.leadout_sector);
    printf("direct CDDA: track=%" PRIu32 " FAD=%" PRIu32
           "..%" PRIu32 "\n", track, track_fad, track_end);

    if(gdrom_direct_cdda_play(
            track, track, 0, CDDA_TRACKS,
            COMMAND_TIMEOUT_MS, &transport) < 0
            || read_status_until(CD_SUB_AUDIO_STATUS_PLAYING, &status) < 0) {
        perror("gdrom_direct_cdda_play track");
        failed = 1;
        goto done;
    }
    print_status("direct track play", &status);
    if(status.track != track || status.fad < track_fad
            || status.fad >= track_end) {
        printf("typed status lies outside the selected track\n");
        failed = 1;
    }

    if(gdrom_direct_get_subcode(raw_q, sizeof(raw_q), CD_SUB_Q_CHANNEL,
                                COMMAND_TIMEOUT_MS, &transport) < 0) {
        perror("gdrom_direct_get_subcode");
        failed = 1;
    }
    else {
        printf("direct raw Q: audio=%#x track=%u index=%u FAD=%u\n",
               raw_q[1], raw_q[5], raw_q[6],
               ((unsigned)raw_q[11] << 16)
                   | ((unsigned)raw_q[12] << 8) | raw_q[13]);
    }

    if(gdrom_direct_cdda_get_status(
            &status, COMMAND_TIMEOUT_MS, &transport) < 0) {
        perror("gdrom_direct_cdda_get_status");
        failed = 1;
        goto done;
    }
    print_status("direct typed status", &status);

    if(gdrom_direct_cdda_pause(COMMAND_TIMEOUT_MS, &transport) < 0
            || read_status_until(CD_SUB_AUDIO_STATUS_PAUSED, &status) < 0) {
        perror("gdrom_direct_cdda_pause");
        failed = 1;
        goto done;
    }
    print_status("direct pause", &status);

    if(gdrom_direct_cdda_resume(COMMAND_TIMEOUT_MS, &transport) < 0
            || read_status_until(CD_SUB_AUDIO_STATUS_PLAYING, &status) < 0) {
        perror("gdrom_direct_cdda_resume");
        failed = 1;
        goto done;
    }

    if(gdrom_direct_cdda_scan(
            false, 0, COMMAND_TIMEOUT_MS, &transport) < 0) {
        perror("gdrom_direct_cdda_scan");
        failed = 1;
        goto done;
    }
    thd_sleep(100);

    if(track_end > track_fad + 76u
            && gdrom_direct_cdda_play(
                track_fad + 1u, track_fad + 76u, 0, CDDA_SECTORS,
                COMMAND_TIMEOUT_MS, &transport) < 0) {
        perror("gdrom_direct_cdda_play FAD");
        failed = 1;
        goto done;
    }

done:
    if(gdrom_direct_cdda_stop(COMMAND_TIMEOUT_MS, &transport) < 0) {
        perror("gdrom_direct_cdda_stop");
        failed = 1;
    }

    if(cdrom_read_toc(&bios_toc, false) != ERR_OK) {
        printf("BIOS TOC failed after direct CDDA controls\n");
        failed = 1;
    }
    else if(!failed && memcmp(&direct_toc, &bios_toc, sizeof(bios_toc))) {
        printf("BIOS/direct low-density TOCs differ after controller reuse\n");
        failed = 1;
    }

    printf("%s: close or reset the console to exit.\n",
           failed ? "FAIL" : "PASS");
    for(;;)
        thd_sleep(1000);
}
