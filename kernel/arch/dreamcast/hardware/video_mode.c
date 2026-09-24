/* KallistiOS ##version##

   video_mode.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/video.h>
#include <dc/pvr.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "video_mode_internal.h"

#define VID_KNOWN_FLAGS \
    (VID_INTERLACE | VID_LINEDOUBLE | VID_PIXELDOUBLE | VID_PAL)
#define VID_TIMING_COUNTER_MAX UINT16_C(1023)

static int valid_cable(int8_t cable_type) {
    return cable_type >= CT_ANY && cable_type <= CT_COMPOSITE;
}

static int valid_pixel_mode(vid_pixel_mode_t pm) {
    return pm >= PM_RGB555 && pm <= PM_RGB0888;
}

static int valid_standard(vid_mode_standard_t standard) {
    return standard >= VID_MODE_STANDARD_DEFAULT &&
           standard <= VID_MODE_STANDARD_50HZ;
}

static int mode_matches_standard(const vid_mode_t *mode,
                                 vid_mode_standard_t standard,
                                 int8_t cable_type) {
    if(cable_type == CT_VGA || standard == VID_MODE_STANDARD_DEFAULT)
        return 1;

    if(standard == VID_MODE_STANDARD_50HZ)
        return !!(mode->flags & VID_PAL);

    return !(mode->flags & VID_PAL);
}

int vid_mode_validate_for_vram(const vid_mode_t *mode, int8_t cable_type,
                               size_t vram_bytes, size_t *frame_bytes) {
    size_t row_bytes, minimum_frame, effective_frame;
    unsigned int bytes_per_pixel;

    if(!mode) {
        errno = EFAULT;
        return -1;
    }

    if(!valid_cable(cable_type) || !valid_cable(mode->cable_type) ||
       !valid_pixel_mode(mode->pm) || !mode->width || !mode->height ||
       !mode->scanlines || !mode->clocks || !mode->fb_count ||
       (mode->flags & ~VID_KNOWN_FLAGS)) {
        errno = EINVAL;
        return -1;
    }

    if(cable_type != CT_ANY && mode->cable_type != CT_ANY &&
       mode->cable_type != cable_type) {
        errno = ENODEV;
        return -1;
    }

    if(cable_type == CT_VGA && (mode->flags & VID_PAL)) {
        errno = ENOTSUP;
        return -1;
    }

    /* Both timing counters are ten-bit hardware fields. VGA line doubling is
       applied by vid_apply_mode(), so validate the effective vertical value
       rather than accepting a mode that would be silently truncated. */
    if(mode->scanlines > VID_TIMING_COUNTER_MAX ||
       mode->clocks > VID_TIMING_COUNTER_MAX ||
       (cable_type == CT_VGA && (mode->flags & VID_LINEDOUBLE) &&
        mode->scanlines > VID_TIMING_COUNTER_MAX / 2u)) {
        errno = ERANGE;
        return -1;
    }

    if((mode->flags & VID_INTERLACE) &&
       (mode->height < 2u || (mode->height & 1u))) {
        errno = EINVAL;
        return -1;
    }

    bytes_per_pixel = vid_pmode_bpp[mode->pm];
    if(mode->width > SIZE_MAX / bytes_per_pixel) {
        errno = EOVERFLOW;
        return -1;
    }

    row_bytes = (size_t)mode->width * bytes_per_pixel;
    if((row_bytes & 7u) || row_bytes / 4u > 1024u ||
       mode->height > 1024u || mode->height > SIZE_MAX / row_bytes) {
        errno = EINVAL;
        return -1;
    }

    minimum_frame = row_bytes * mode->height;
    minimum_frame = (minimum_frame + 3u) & ~(size_t)3u;
    effective_frame = mode->fb_size ? mode->fb_size : minimum_frame;

    if(effective_frame < minimum_frame || (effective_frame & 3u) ||
       !vram_bytes || effective_frame > vram_bytes ||
       mode->fb_count > vram_bytes / effective_frame) {
        errno = ENOMEM;
        return -1;
    }

    if(frame_bytes)
        *frame_bytes = effective_frame;

    return 0;
}

int vid_mode_validate(const vid_mode_t *mode, int8_t cable_type) {
    return vid_mode_validate_for_vram(mode, cable_type, PVR_RAM_SIZE, NULL);
}

int vid_mode_resolve_from_table(const vid_mode_t *table, size_t table_count,
                                int dm, vid_pixel_mode_t pm,
                                int8_t cable_type,
                                vid_mode_standard_t standard,
                                size_t vram_bytes, vid_mode_t *mode) {
    size_t frame_bytes;
    int multibuffer;
    int requested_mode;

    if(!table || !mode) {
        errno = EFAULT;
        return -1;
    }

    if(!valid_cable(cable_type) || cable_type == CT_ANY ||
       !valid_pixel_mode(pm) || !valid_standard(standard) ||
       (dm & ~(DM_MULTIBUFFER | 0x1fffu))) {
        errno = EINVAL;
        return -1;
    }

    multibuffer = !!(dm & DM_MULTIBUFFER);
    requested_mode = dm & ~DM_MULTIBUFFER;

    if(requested_mode > DM_INVALID &&
       (size_t)requested_mode < table_count) {
        memcpy(mode, &table[requested_mode], sizeof(*mode));
    }
    else if(requested_mode >= DM_GENERIC_FIRST &&
            requested_mode <= DM_GENERIC_LAST) {
        int pass;
        size_t i;
        int found = 0;

        /* Prefer an exact cable entry before falling back to CT_ANY. This
           makes selection independent of incidental table order. */
        for(pass = 0; pass < 2 && !found; ++pass) {
            for(i = 1; i < table_count; ++i) {
                const vid_mode_t *candidate = &table[i];
                int cable_match = pass == 0 ?
                    candidate->cable_type == cable_type :
                    candidate->cable_type == CT_ANY;

                if(candidate->generic != requested_mode || !cable_match ||
                   !mode_matches_standard(candidate, standard, cable_type))
                    continue;

                memcpy(mode, candidate, sizeof(*mode));
                found = 1;
                break;
            }
        }

        if(!found) {
            errno = ENOTSUP;
            return -1;
        }
    }
    else {
        errno = EINVAL;
        return -1;
    }

    mode->pm = pm;
    mode->fb_curr = 0;
    mode->fb_count = 1;
    mode->fb_size = 0;

    if(vid_mode_validate_for_vram(mode, cable_type, vram_bytes,
                                  &frame_bytes) < 0)
        return -1;

    mode->cable_type = cable_type;
    mode->fb_size = frame_bytes;
    if(multibuffer) {
        size_t framebuffer_count = vram_bytes / frame_bytes;

        mode->fb_count = framebuffer_count > UINT16_MAX ? UINT16_MAX :
                         (uint16_t)framebuffer_count;
    }

    return 0;
}

int vid_mode_resolve(int dm, vid_pixel_mode_t pm, int8_t cable_type,
                     vid_mode_standard_t standard, vid_mode_t *mode) {
    return vid_mode_resolve_from_table(vid_builtin, DM_SENTINEL, dm, pm,
                                       cable_type, standard, PVR_RAM_SIZE,
                                       mode);
}
