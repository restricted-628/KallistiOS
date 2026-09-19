/* KallistiOS ##version##

   dreameye-sd.c
   Copyright (C) 2013 Lawrence Sebald
   Copyright (C) 2026 Joseph Black

   This example simply dumps all the images on the first connected Dreameye to
   the SD card. It creates a new directory and saves the images in it.
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include <dc/sd.h>
#include <kos/blockdev.h>
#include <ext2/fs_ext2.h>
#include <dc/maple.h>
#include <dc/maple/dreameye.h>
#include <kos/dbgio.h>

int main(int argc, char *argv[]) {
    maple_device_t *dreameye;
    dreameye_status_t status;
    uint8_t *buf = NULL;
    int size, err;
    FILE *fp;
    int img_count, i;
    char fn[64];
    kos_blockdev_t sd_dev;
    uint8_t partition_type;

    /* We're not using these, obviously... */
    (void)argc;
    (void)argv;

    /* Comment this out if you'd rather that debug output went to dcload. Of
       course, you'll need to be using dcload-ip, but you should have already
       known that. ;-) */
    dbgio_dev_select("fb");

    printf("KallistiOS Dreameye Image Dump program\n");
    printf("Attempting to find a connected Dreameye device...\n");

    dreameye = maple_enum_type(0, MAPLE_FUNC_CAMERA);

    if(!dreameye) {
        printf("Couldn't find any attached devices, bailing out.\n");
        exit(EXIT_FAILURE);
    }

    dreameye = maple_enum_dev(dreameye->port, 1);

    if(!dreameye || !(dreameye->info.functions & MAPLE_FUNC_CAMERA)) {
        printf("The camera's stored-image unit is unavailable.\n");
        exit(EXIT_FAILURE);
    }

    printf("Attempting to grab the number of saved images...\n");

    if(dreameye_get_image_count(dreameye, 1) != MAPLE_EOK ||
       dreameye_get_status(dreameye, &status) < 0 ||
       !status.image_count_valid) {
        printf("The camera did not return a valid image count.\n");
        exit(EXIT_FAILURE);
    }

    printf("Image count: %d\n", status.image_count);
    img_count = status.image_count;

    if(img_count > 0x20) {
        printf("Image count exceeds the stored-image index range.\n");
        exit(EXIT_FAILURE);
    }

    /* Initialize the low-level SD card stuff. */
    if(sd_init()) {
        printf("Could not initialize the SD card. Please make sure that you "
               "have an SD card adapter plugged in and an SD card inserted.\n");
        exit(EXIT_FAILURE);
    }

    /* Grab the block device for the first partition on the SD card. Note that
       you must have the SD card formatted with an MBR partitioning scheme. */
    if(sd_blockdev_for_partition(0, &sd_dev, &partition_type)) {
        printf("Could not find the first partition on the SD card!\n");
        exit(EXIT_FAILURE);
    }

    /* Check to see if the MBR says that we have a Linux partition. */
    if(partition_type != 0x83) {
        printf("MBR indicates a non-ext2 filesystem. Will try to mount "
               "anyway\n");
    }

    /* Initialize fs_ext2 and attempt to mount the device. */
    if(fs_ext2_init()) {
        printf("Could not initialize fs_ext2!\n");
        exit(EXIT_FAILURE);
    }

    if(fs_ext2_mount("/sd", &sd_dev, FS_EXT2_MOUNT_READWRITE)) {
        printf("Could not mount SD card as ext2fs. Please make sure the card "
               "has been properly formatted.\n");
        exit(EXIT_FAILURE);
    }

    /* Try to make a "dreameye" directory on the root of the card and move to
       the new directory. */
    if(mkdir("/sd/dreameye", 0777) && errno != EEXIST) {
        printf("Cannot create a dreameye directory: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if(chdir("/sd/dreameye")) {
        printf("Cannot set current working directory: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    for(i = 0; i < img_count; ++i) {
        printf("Reading image %d...\n", i + 1);
        err = dreameye_get_image_timed(
            dreameye, (uint8_t)(i + 2), &buf, &size,
            DREAMEYE_DEFAULT_TRANSFER_TIMEOUT);

        if(err != MAPLE_EOK) {
            printf("Error was: %d\n", err);
            free(buf);
            continue;
        }

        printf("Image received successfully, size %d bytes\n", size);
        snprintf(fn, sizeof(fn), "image%02d.jpg", i + 1);

        if(!(fp = fopen(fn, "wb"))) {
            printf("Cannot open /sd/dreameye/%s: %s\n", fn, strerror(errno));
            free(buf);
            continue;
        }

        if(fwrite(buf, 1, size, fp) != (size_t)size) {
            printf("Cannot write image to file: %s\n", strerror(errno));
            free(buf);
            fclose(fp);
            continue;
        }

        fclose(fp);
        free(buf);
    }

    /* Clean up the filesystem and everything else */
    if(fs_ext2_unmount("/sd"))
        printf("Warning: could not unmount /sd: %s\n", strerror(errno));
    fs_ext2_shutdown();
    sd_shutdown();

    printf("Complete!\n");
    return 0;
}
