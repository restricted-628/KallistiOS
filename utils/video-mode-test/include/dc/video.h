#ifndef __VIDEO_MODE_TEST_DC_VIDEO_H
#define __VIDEO_MODE_TEST_DC_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#define CT_ANY          -1
#define CT_VGA           0
#define CT_NONE          1
#define CT_RGB           2
#define CT_COMPOSITE     3

typedef enum vid_pixel_mode {
    PM_RGB555 = 0,
    PM_RGB565 = 1,
    PM_RGB888P = 2,
    PM_RGB0888 = 3,
    PM_RGB888 = 3
} vid_pixel_mode_t;

static const uint8_t vid_pmode_bpp[4] = { 2, 2, 3, 4 };

typedef enum vid_display_mode_generic {
    DM_GENERIC_FIRST = 0x1000,
    DM_320x240 = 0x1000,
    DM_640x480,
    DM_256x256,
    DM_768x480,
    DM_768x576,
    DM_GENERIC_LAST = DM_768x576
} vid_display_mode_generic_t;

#define DM_MULTIBUFFER 0x2000

typedef enum vid_display_mode {
    DM_INVALID = 0,
    DM_320x240_VGA = 1,
    DM_320x240_NTSC,
    DM_640x480_VGA,
    DM_640x480_NTSC_IL,
    DM_640x480_PAL_IL,
    DM_256x256_PAL_IL,
    DM_768x480_NTSC_IL,
    DM_768x576_PAL_IL,
    DM_768x480_PAL_IL,
    DM_320x240_PAL,
    DM_SENTINEL,
    DM_MODE_COUNT
} vid_display_mode_t;

typedef enum vid_mode_standard {
    VID_MODE_STANDARD_DEFAULT = 0,
    VID_MODE_STANDARD_60HZ,
    VID_MODE_STANDARD_50HZ
} vid_mode_standard_t;

#define VID_INTERLACE   0x00000001
#define VID_LINEDOUBLE  0x00000002
#define VID_PIXELDOUBLE 0x00000004
#define VID_PAL         0x00000008

typedef struct vid_mode {
    uint16_t generic;
    uint16_t width;
    uint16_t height;
    uint32_t flags;
    int8_t cable_type;
    vid_pixel_mode_t pm;
    uint16_t scanlines;
    uint16_t clocks;
    uint16_t bitmapx;
    uint16_t bitmapy;
    uint16_t scanint1;
    uint16_t scanint2;
    uint16_t borderx1;
    uint16_t borderx2;
    uint16_t bordery1;
    uint16_t bordery2;
    uint16_t fb_curr;
    uint16_t fb_count;
    size_t fb_size;
} vid_mode_t;

extern vid_mode_t vid_builtin[DM_MODE_COUNT];

int vid_mode_validate(const vid_mode_t *mode, int8_t cable_type);
int vid_mode_resolve(int dm, vid_pixel_mode_t pm, int8_t cable_type,
                     vid_mode_standard_t standard, vid_mode_t *mode);

#endif
