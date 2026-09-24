/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Host-side generation of a headerless, little-endian, linear RGB565 texture.
*/
#include <stdio.h>
#include <stdlib.h>
#include "texture-pattern.h"

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: %s texture.bin\n", argv[0]);
        return EXIT_FAILURE;
    }
    FILE *out = fopen(argv[1], "wb");
    if(!out) { perror(argv[1]); return EXIT_FAILURE; }
    for(unsigned int y = 0; y < TEXTURE_HEIGHT; ++y)
        for(unsigned int x = 0; x < TEXTURE_WIDTH; ++x) {
            uint16_t pixel = texture_pixel(x, y);
            if(fputc(pixel & 255, out) == EOF || fputc(pixel >> 8, out) == EOF) {
                perror("texture write");
                fclose(out);
                return EXIT_FAILURE;
            }
        }
    if(fclose(out)) { perror("texture close"); return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
