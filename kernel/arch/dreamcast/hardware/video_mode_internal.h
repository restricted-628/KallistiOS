/* KallistiOS ##version##

   video_mode_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __VIDEO_MODE_INTERNAL_H
#define __VIDEO_MODE_INTERNAL_H

#include <dc/video.h>

#include <stddef.h>
#include <stdint.h>

int vid_mode_validate_for_vram(const vid_mode_t *mode, int8_t cable_type,
                               size_t vram_bytes, size_t *frame_bytes);

int vid_mode_resolve_from_table(const vid_mode_t *table, size_t table_count,
                                int dm, vid_pixel_mode_t pm,
                                int8_t cable_type,
                                vid_mode_standard_t standard,
                                size_t vram_bytes, vid_mode_t *mode);

#endif
