/* KallistiOS ##version##

   BootROM media-recognition and disc-identity validation.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/cdrom.h>

#include <stdio.h>
#include <string.h>

#define RECOGNITION_TIMEOUT_MS 4000u

KOS_INIT_FLAGS(INIT_DEFAULT);

static void print_disc_id(const char *label, const cdrom_disc_id_t *id) {
    printf("%s: product='%s' disc=%lu/%lu\n", label, id->product_id,
           (unsigned long)id->disc_number,
           (unsigned long)id->disc_count);
}

int main(int argc, char **argv) {
    cdrom_disc_id_t boot_id;
    cdrom_disc_id_t current_id;
    cd_toc_t toc;
    uint8_t first_read[32];
    file_t fd;
    int recognition;
    bool dedicated_disc = false;
    int failed = 0;

    (void)argc;
    (void)argv;

    puts("KOS BootROM media recognition validation");

    if(cdrom_get_boot_disc_id(&boot_id) < 0) {
        perror("cdrom_get_boot_disc_id");
        failed = 1;
        goto done;
    }
    print_disc_id("boot", &boot_id);

    recognition = cdrom_media_recognize(RECOGNITION_TIMEOUT_MS);
    if(recognition < 0) {
        perror("cdrom_media_recognize");
        failed = 1;
        goto done;
    }
    if(recognition == 0) {
        dedicated_disc = true;
        if(cdrom_get_current_disc_id(&current_id) < 0) {
            perror("cdrom_get_current_disc_id");
            failed = 1;
            goto done;
        }
        print_disc_id("current", &current_id);

        if(boot_id.disc_number != current_id.disc_number
                || boot_id.disc_count != current_id.disc_count
                || strcmp(boot_id.product_id, current_id.product_id) != 0) {
            puts("boot and current identities differ for the unchanged disc");
            failed = 1;
        }
    }
    else {
        puts("BootROM classified this CD-R/MIL-CD image as a non-dedicated "
             "medium; current-disc ID is intentionally unavailable");
    }

    if(cdrom_read_toc(&toc, false) != ERR_OK) {
        puts("BIOS TOC failed after media recognition");
        failed = 1;
    }
    else {
        printf("BIOS reuse: tracks=%u-%u\n",
               (unsigned)TOC_TRACK(toc.first),
               (unsigned)TOC_TRACK(toc.last));
    }

    fd = fs_open("/cd/1ST_READ.BIN", O_RDONLY);
    if(fd < 0) {
        perror("fs_open /cd/1ST_READ.BIN");
        failed = 1;
    }
    else {
        ssize_t amount = fs_read(fd, first_read, sizeof(first_read));

        if(amount != (ssize_t)sizeof(first_read)) {
            printf("BIOS /cd read returned %ld bytes\n", (long)amount);
            failed = 1;
        }
        else {
            puts("BIOS /cd remount/read succeeded after media recognition");
        }
        fs_close(fd);
    }

done:
    if(failed)
        puts("RESULT: FAIL");
    else if(dedicated_disc)
        puts("RESULT: PASS");
    else
        puts("RESULT: PARTIAL (dedicated-disc identity not applicable)");
    puts("Close or reset the console to exit.");
    for(;;)
        thd_sleep(1000);
}
