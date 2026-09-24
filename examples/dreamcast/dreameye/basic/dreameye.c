/* KallistiOS ##version##

   dreameye.c
   Copyright (C) 2009 Lawrence Sebald
   Copyright (C) 2026 Joseph Black
*/

#include <stdio.h>
#include <stdlib.h>

#include <dc/maple.h>
#include <dc/maple/dreameye.h>

int main(int argc, char *argv[]) {
    maple_device_t *dreameye;
    dreameye_status_t status;
    uint8_t *buf = NULL;
    int size = 0;
    int err;
    FILE *fp;

    (void)argc;
    (void)argv;

    printf("KallistiOS Dreameye Test program\n");
    printf("Attempting to find a connected Dreameye device...\n");

    dreameye = maple_enum_type(0, MAPLE_FUNC_CAMERA);

    if(!dreameye) {
        printf("Couldn't find any attached devices, bailing out.\n");
        return EXIT_FAILURE;
    }

    /* Stored-image transfers start on camera unit 1. Enumeration may have
       returned another camera unit first. */
    dreameye = maple_enum_dev(dreameye->port, 1);

    if(!dreameye || !(dreameye->info.functions & MAPLE_FUNC_CAMERA)) {
        printf("The camera's stored-image unit is unavailable.\n");
        return EXIT_FAILURE;
    }

    printf("Attempting to grab the number of saved images...\n");

    if(dreameye_get_image_count(dreameye, 1) != MAPLE_EOK ||
       dreameye_get_status(dreameye, &status) < 0 ||
       !status.image_count_valid) {
        printf("The camera did not return a valid image count.\n");
        return EXIT_FAILURE;
    }

    printf("Image count: %d\n", status.image_count);

    if(status.image_count == 0) {
        printf("There are no stored images to read.\n");
        return EXIT_SUCCESS;
    }

    printf("Attempting to grab the first image.\n");
    err = dreameye_get_image_timed(dreameye, 2, &buf, &size,
                                   DREAMEYE_DEFAULT_TRANSFER_TIMEOUT);

    if(err != MAPLE_EOK) {
        printf("Error was: %d\n", err);
        return EXIT_FAILURE;
    }

    printf("Image received successfully, size %d bytes\n", size);

    fp = fopen("/pc/image.jpg", "wb");

    if(!fp) {
        printf("Could not open /pc/image.jpg for writing\n");
        free(buf);
        return EXIT_FAILURE;
    }

    if(fwrite(buf, 1, (size_t)size, fp) != (size_t)size) {
        printf("Could not write the complete image.\n");
        fclose(fp);
        free(buf);
        return EXIT_FAILURE;
    }

    if(fclose(fp) != 0) {
        printf("Could not finish /pc/image.jpg.\n");
        free(buf);
        return EXIT_FAILURE;
    }

    free(buf);

    printf("That's all for now.\n");
    return EXIT_SUCCESS;
}
