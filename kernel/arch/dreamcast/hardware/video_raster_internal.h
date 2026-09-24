/* KallistiOS ##version##

   video_raster_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __VIDEO_RASTER_INTERNAL_H
#define __VIDEO_RASTER_INTERNAL_H

#include <stdint.h>

void vid_raster_mode_change_begin(void);
void vid_raster_mode_changed(uint16_t maximum_scanline);
void vid_raster_shutdown(void);

#endif /* __VIDEO_RASTER_INTERNAL_H */
