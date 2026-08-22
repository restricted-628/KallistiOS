/* KallistiOS ##version##

   video-mode-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/video.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "video_mode_internal.h"

#define VRAM_BYTES (8u * 1024u * 1024u)

static int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

#define MODE(generic_, width_, height_, flags_, cable_) \
    { (generic_), (width_), (height_), (flags_), (cable_), PM_RGB555, \
      524, 857, 0, 0, 21, 260, 0, 800, 0, 520, 0, 1, 0 }

vid_mode_t vid_builtin[DM_MODE_COUNT] = {
    { 0 },
    MODE(DM_320x240, 320, 240, VID_PIXELDOUBLE | VID_LINEDOUBLE, CT_VGA),
    MODE(DM_320x240, 320, 240, VID_PIXELDOUBLE | VID_LINEDOUBLE, CT_ANY),
    MODE(DM_640x480, 640, 480, 0, CT_VGA),
    MODE(DM_640x480, 640, 480, VID_INTERLACE, CT_ANY),
    MODE(DM_640x480, 640, 480, VID_INTERLACE | VID_PAL, CT_ANY),
    MODE(DM_256x256, 256, 256,
         VID_PIXELDOUBLE | VID_LINEDOUBLE | VID_INTERLACE | VID_PAL, CT_ANY),
    MODE(DM_768x480, 768, 480, VID_INTERLACE, CT_ANY),
    MODE(DM_768x576, 768, 576, VID_INTERLACE | VID_PAL, CT_ANY),
    MODE(DM_768x480, 768, 480, VID_INTERLACE | VID_PAL, CT_ANY),
    MODE(DM_320x240, 320, 240,
         VID_PIXELDOUBLE | VID_LINEDOUBLE | VID_PAL, CT_ANY),
    { 0 }
};

static void test_standard_selection(void) {
    vid_mode_t mode;

    CHECK(vid_mode_resolve(DM_640x480, PM_RGB565, CT_VGA,
                           VID_MODE_STANDARD_50HZ, &mode) == 0);
    CHECK(mode.generic == DM_640x480 && mode.cable_type == CT_VGA);
    CHECK(!(mode.flags & VID_PAL));

    CHECK(vid_mode_resolve(DM_640x480, PM_RGB565, CT_COMPOSITE,
                           VID_MODE_STANDARD_DEFAULT, &mode) == 0);
    CHECK(!(mode.flags & VID_PAL));

    CHECK(vid_mode_resolve(DM_640x480, PM_RGB565, CT_COMPOSITE,
                           VID_MODE_STANDARD_50HZ, &mode) == 0);
    CHECK(mode.flags & VID_PAL);

    CHECK(vid_mode_resolve(DM_768x480, PM_RGB565, CT_RGB,
                           VID_MODE_STANDARD_60HZ, &mode) == 0);
    CHECK(!(mode.flags & VID_PAL));
    CHECK(vid_mode_resolve(DM_768x480, PM_RGB565, CT_RGB,
                           VID_MODE_STANDARD_50HZ, &mode) == 0);
    CHECK(mode.flags & VID_PAL);

    errno = 0;
    CHECK(vid_mode_resolve(DM_768x576, PM_RGB565, CT_COMPOSITE,
                           VID_MODE_STANDARD_60HZ, &mode) < 0);
    CHECK(errno == ENOTSUP);
}

static void test_cable_and_multibuffer(void) {
    vid_mode_t mode;
    vid_mode_t tiny_table[2] = {
        { 0 },
        MODE(DM_320x240, 4, 1, 0, CT_ANY)
    };

    errno = 0;
    CHECK(vid_mode_resolve(DM_640x480_VGA, PM_RGB565, CT_COMPOSITE,
                           VID_MODE_STANDARD_DEFAULT, &mode) < 0);
    CHECK(errno == ENODEV);

    CHECK(vid_mode_resolve(DM_640x480 | DM_MULTIBUFFER, PM_RGB565,
                           CT_COMPOSITE, VID_MODE_STANDARD_60HZ, &mode) == 0);
    CHECK(mode.fb_size == 640u * 480u * 2u);
    CHECK(mode.fb_count == VRAM_BYTES / mode.fb_size);
    CHECK(mode.cable_type == CT_COMPOSITE);

    CHECK(vid_mode_resolve_from_table(tiny_table, 2,
                                      DM_320x240_VGA | DM_MULTIBUFFER,
                                      PM_RGB565, CT_COMPOSITE,
                                      VID_MODE_STANDARD_DEFAULT, VRAM_BYTES,
                                      &mode) == 0);
    CHECK(mode.fb_count == UINT16_MAX);

    errno = 0;
    CHECK(vid_mode_resolve(DM_256x256, PM_RGB565, CT_VGA,
                           VID_MODE_STANDARD_DEFAULT, &mode) < 0);
    CHECK(errno == ENOTSUP);
}

static void test_validation(void) {
    vid_mode_t mode = MODE(DM_640x480, 640, 480, VID_INTERLACE, CT_ANY);
    size_t frame_bytes;

    CHECK(vid_mode_validate_for_vram(&mode, CT_COMPOSITE, VRAM_BYTES,
                                     &frame_bytes) == 0);
    CHECK(frame_bytes == 640u * 480u * 2u);

    mode.pm = (vid_pixel_mode_t)4;
    errno = 0;
    CHECK(vid_mode_validate_for_vram(&mode, CT_COMPOSITE, VRAM_BYTES,
                                     NULL) < 0);
    CHECK(errno == EINVAL);
    mode.pm = PM_RGB565;

    mode.height = 479;
    errno = 0;
    CHECK(vid_mode_validate_for_vram(&mode, CT_COMPOSITE, VRAM_BYTES,
                                     NULL) < 0);
    CHECK(errno == EINVAL);
    mode.height = 480;

    mode.fb_count = 0;
    errno = 0;
    CHECK(vid_mode_validate_for_vram(&mode, CT_COMPOSITE, VRAM_BYTES,
                                     NULL) < 0);
    CHECK(errno == EINVAL);
    mode.fb_count = 1;

    mode.fb_size = 1024;
    errno = 0;
    CHECK(vid_mode_validate_for_vram(&mode, CT_COMPOSITE, VRAM_BYTES,
                                     NULL) < 0);
    CHECK(errno == ENOMEM);
    mode.fb_size = 0;

    mode.flags |= UINT32_C(0x100);
    errno = 0;
    CHECK(vid_mode_validate_for_vram(&mode, CT_COMPOSITE, VRAM_BYTES,
                                     NULL) < 0);
    CHECK(errno == EINVAL);
    mode.flags &= ~UINT32_C(0x100);

    mode.flags |= VID_PAL;
    errno = 0;
    CHECK(vid_mode_validate_for_vram(&mode, CT_VGA, VRAM_BYTES, NULL) < 0);
    CHECK(errno == ENOTSUP);

    errno = 0;
    CHECK(vid_mode_validate_for_vram(NULL, CT_VGA, VRAM_BYTES, NULL) < 0);
    CHECK(errno == EFAULT);
}

static void test_bad_requests(void) {
    vid_mode_t mode;

    errno = 0;
    CHECK(vid_mode_resolve(DM_640x480, (vid_pixel_mode_t)4, CT_COMPOSITE,
                           VID_MODE_STANDARD_DEFAULT, &mode) < 0);
    CHECK(errno == EINVAL);

    errno = 0;
    CHECK(vid_mode_resolve(DM_640x480, PM_RGB565, CT_ANY,
                           VID_MODE_STANDARD_DEFAULT, &mode) < 0);
    CHECK(errno == EINVAL);

    errno = 0;
    CHECK(vid_mode_resolve(-1, PM_RGB565, CT_COMPOSITE,
                           VID_MODE_STANDARD_DEFAULT, &mode) < 0);
    CHECK(errno == EINVAL);
}

int main(void) {
    test_standard_selection();
    test_cable_and_multibuffer();
    test_validation();
    test_bad_requests();

    if(failures) {
        fprintf(stderr, "%d video mode test(s) failed\n", failures);
        return 1;
    }

    puts("Video mode tests passed");
    return 0;
}
