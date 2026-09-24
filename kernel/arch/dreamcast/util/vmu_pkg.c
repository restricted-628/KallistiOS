/* KallistiOS ##version##

   vmu_pkg.c
   (c)2002 Megan Potter
   Copyright (C) 2026 Joseph Black
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dc/vmu_pkg.h>
#include <kos/dbglog.h>
#include <kos/fs.h>
#include <kos/regfield.h>

/*

VMU data files can be stored raw, but if you want to interact with the
rest of the DC world, it's much better to package them in a nice data
file format. This module takes care of that for you.

Thanks to Marcus Comstedt for this information.

*/

struct ico_header {
    uint16_t resv;
    uint16_t type;
    uint16_t nb_images;
};

struct ico_dir {
    uint8_t width;
    uint8_t height;
    uint8_t nb_colors;
    uint8_t resv;
    uint16_t nb_planes;
    uint16_t bpp;
    uint32_t size;
    uint32_t offset;
};

struct bmp_dib_header {
    uint32_t hdr_size;
    int32_t width;
    int32_t height;
    uint16_t nb_planes;
    uint16_t bpp;
    uint32_t comp;
    uint32_t size;
    uint32_t hppm;
    uint32_t vppm;
    uint32_t nb_colors;
    uint32_t important_colors;
};

static unsigned int pal_get_map(uint32_t *pal, const uint32_t *curr_pal,
                                uint8_t *map, unsigned int nb_colors) {
    unsigned int i, j;

    for(i = 0; i < 16; i++) {
        for(j = 0; j < nb_colors; j++)
            if(curr_pal[i] == pal[j])
                break;

        if(j < nb_colors) {
            /* Found the color in our palette */
            map[i] = j;
            continue;
        }

        if(nb_colors == 15) {
            /* No colors left :(
             * Note that we limit to 15 colors to leave the 16th color for
             * transparent pixels. */
            return 0;
        }

        /* Add the new color to our palette */
        pal[nb_colors] = curr_pal[i];
        map[i] = nb_colors++;
    }

    return nb_colors;
}

static uint16_t __pure argb8888_to_argb4444(uint32_t px) {
    return 0xf000 |
        ((px >> 12) & 0x0f00) |
        ((px >> 8) & 0x00f0) |
        ((px >> 4) & 0x000f);
}

static void vmu_pkg_load_palette(vmu_pkg_t *pkg, const uint32_t *pal,
                                 unsigned int nb_colors) {
    unsigned int i;

    for(i = 0; i < nb_colors; i++)
        pkg->icon_pal[i] = argb8888_to_argb4444(pal[i]);

    pkg->icon_pal[15] = 0x0; /* Transparent pixel */
}

int vmu_pkg_load_icon(vmu_pkg_t *pkg, const char *icon_fn) {
    uint8_t px, pxh, pxl, pal_map[16];
    unsigned int i, j, x, y, nb_colors = 0;
    struct bmp_dib_header dib;
    uint32_t palette[16], curr_palette[16];
    struct ico_header hdr;
    struct ico_dir *dir;
    uint8_t and_mask;
    uint8_t frame[512];
    file_t fd;

    if(!pkg->icon_cnt || !pkg->icon_data) {
        dbglog(DBG_ERROR, "vmu_pkg_load_icon: vmu_pkg_t icon not preallocated\n");
        return -1;
    }

    fd = fs_open(icon_fn, O_RDONLY);
    if(fd == FILEHND_INVALID)
        return fd;

    fs_read(fd, &hdr, sizeof(hdr));
    if(hdr.resv != 0 || hdr.type != 1) {
        dbglog(DBG_ERROR, "vmu_pkg_load_icon: Invalid .ico header\n");
        goto out_err_close;
    }

    /* The ICO has less frames than preallocated? No problem */
    if(hdr.nb_images < pkg->icon_cnt)
        pkg->icon_cnt = hdr.nb_images;

    /* The ICO has more frames than preallocated? Warn and continue */
    if(hdr.nb_images > pkg->icon_cnt) {
        dbglog(DBG_WARNING, "vmu_pkg_load_icon: .ico file has %u frames but we only have space for %u\n",
               hdr.nb_images, pkg->icon_cnt);
    }

    dir = alloca(pkg->icon_cnt * sizeof(*dir));

    for(i = 0; i < hdr.nb_images; i++) {
        fs_read(fd, &dir[i], sizeof(*dir));

        if(dir->width != 32 || dir->height != 32 || dir->bpp != 4) {
            dbglog(DBG_ERROR, "vmu_pkg_load_icon: Invalid .ico width, height or bpp\n");
            goto out_err_close;
        }
    }

    for(i = 0; i < (unsigned int)pkg->icon_cnt; i++) {
        fs_read(fd, &dib, sizeof(dib));

        if(dib.hdr_size != 40) {
            dbglog(DBG_ERROR, "vmu_pkg_load_icon: Invalid DIB header for frame %u\n", i);
            goto out_err_close;
        }

        if(dib.comp != 0) {
            dbglog(DBG_ERROR, "vmu_pkg_load_icon: Only uncompressed .ico are supported.\n");
            goto out_err_close;
        }

        fs_read(fd, curr_palette, sizeof(curr_palette));
        nb_colors = pal_get_map(palette, curr_palette, pal_map, nb_colors);
        if(!nb_colors) {
            dbglog(DBG_ERROR, "vmu_pkg_load_icon: .ico has > 15 colors.\n");
            goto out_err_close;
        }

        /* Read frame data */
        fs_read(fd, frame, sizeof(frame));

        /* Rewrite indices according to the map and the AND mask */
        for(y = 0; y < 32; y++) {
            for(x = 0; x < 16; x += 4) {
                fs_read(fd, &and_mask, sizeof(and_mask));

                for(j = 0; j < 4; j++) {
                    px = frame[y * 16 + x + j];
                    pxh = px >> 4;
                    pxl = px & 0x0f;

                    if(and_mask & BIT(7 - j * 2))
                        pxh = 15;
                    else
                        pxh = pal_map[pxh];
                    if(and_mask & BIT(6 - j * 2))
                        pxl = 15;
                    else
                        pxl = pal_map[pxl];

                    pkg->icon_data[i * 512 + 496 - 16 * y + x + j] = (pxh << 4) | pxl;
                }
            }
        }
    }

    vmu_pkg_load_palette(pkg, palette, nb_colors);

    fs_close(fd);

    return 0;

out_err_close:
    fs_close(fd);

    return -1;
}
