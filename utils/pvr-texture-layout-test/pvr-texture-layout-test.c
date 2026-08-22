/* KallistiOS ##version##

   Host-side PVR texture surface layout tests.
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include <dc/pvr.h>

static void expect_level(const pvr_txr_surface_t *surface, uint32_t level,
                         uint32_t width, uint32_t height, size_t offset,
                         size_t byte_size) {
    pvr_txr_level_info_t info;

    assert(pvr_txr_surface_get_level(surface, level, &info) == 0);
    assert(info.width == width);
    assert(info.height == height);
    assert(info.offset == offset);
    assert(info.byte_size == byte_size);
}

static void test_plain_layouts(void) {
    pvr_txr_surface_t surface;

    assert(pvr_txr_surface_init(&surface, 64, 32,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, false) == 0);
    assert(surface.byte_size == 4096);
    assert(surface.data_size == 4096);
    assert(surface.codebook_size == 0);
    assert(surface.mip_levels == 1);
    expect_level(&surface, 0, 64, 32, 0, 4096);

    assert(pvr_txr_surface_init(&surface, 640, 512,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_STRIDE, false) == 0);
    assert(surface.byte_size == 655360);
    expect_level(&surface, 0, 640, 512, 0, 655360);
}

static void test_mipmap_layouts(void) {
    pvr_txr_surface_t surface;

    assert(pvr_txr_surface_init(&surface, 8, 8,
                                PVR_TXR_SURFACE_ARGB1555,
                                PVR_TXR_SURFACE_TWIDDLED, true) == 0);
    assert(surface.byte_size == 176);
    assert(surface.mip_levels == 4);
    expect_level(&surface, 0, 8, 8, 48, 128);
    expect_level(&surface, 1, 4, 4, 16, 32);
    expect_level(&surface, 2, 2, 2, 8, 8);
    expect_level(&surface, 3, 1, 1, 6, 2);

    assert(pvr_txr_surface_init(&surface, 8, 8,
                                PVR_TXR_SURFACE_PAL4BPP,
                                PVR_TXR_SURFACE_TWIDDLED, true) == 0);
    assert(surface.byte_size == 44);
    expect_level(&surface, 0, 8, 8, 12, 32);
    expect_level(&surface, 3, 1, 1, 1, 1);

    assert(pvr_txr_surface_init(&surface, 1024, 1024,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, true) == 0);
    assert(surface.byte_size == 2796208);
    expect_level(&surface, 0, 1024, 1024, 699056, 2097152);
}

static void test_vq_layouts(void) {
    pvr_txr_surface_t surface;

    assert(pvr_txr_surface_init(&surface, 64, 64,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_VQ, false) == 0);
    assert(surface.codebook_size == 2048);
    assert(surface.data_size == 1024);
    assert(surface.byte_size == 3072);
    expect_level(&surface, 0, 64, 64, 2048, 1024);

    assert(pvr_txr_surface_init(&surface, 64, 32,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_VQ, false) == 0);
    assert(surface.byte_size == 2560);
    expect_level(&surface, 0, 64, 32, 2048, 512);

    assert(pvr_txr_surface_init(&surface, 8, 8,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_VQ, true) == 0);
    assert(surface.byte_size == 2070);
    expect_level(&surface, 0, 8, 8, 2054, 16);
    expect_level(&surface, 3, 1, 1, 2048, 1);
}

static void test_yuv_input_layouts(void) {
    pvr_txr_surface_t surface;
    size_t byte_size = 0;

    assert(pvr_txr_surface_init(&surface, 16, 16,
                                PVR_TXR_SURFACE_YUV422,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(pvr_txr_surface_yuv_input_size(&surface, PVR_TXR_YUV420,
                                          &byte_size) == 0);
    assert(byte_size == 384);
    assert(pvr_txr_surface_yuv_input_size(&surface, PVR_TXR_YUV422,
                                          &byte_size) == 0);
    assert(byte_size == 512);

    assert(pvr_txr_surface_init(&surface, 1024, 1024,
                                PVR_TXR_SURFACE_YUV422,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(pvr_txr_surface_yuv_input_size(&surface, PVR_TXR_YUV420,
                                          &byte_size) == 0);
    assert(byte_size == 1572864);
    assert(pvr_txr_surface_yuv_input_size(&surface, PVR_TXR_YUV422,
                                          &byte_size) == 0);
    assert(byte_size == 2097152);

    assert(pvr_txr_surface_init(&surface, 16, 16,
                                PVR_TXR_SURFACE_YUV422,
                                PVR_TXR_SURFACE_TWIDDLED, false) == 0);
    errno = 0;
    assert(pvr_txr_surface_yuv_input_size(&surface, PVR_TXR_YUV420,
                                          &byte_size) == -1);
    assert(errno == ENOTSUP);

    assert(pvr_txr_surface_init(&surface, 8, 8,
                                PVR_TXR_SURFACE_YUV422,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    errno = 0;
    assert(pvr_txr_surface_yuv_input_size(&surface, PVR_TXR_YUV420,
                                          &byte_size) == -1);
    assert(errno == EINVAL);

    assert(pvr_txr_surface_init(&surface, 16, 16,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    errno = 0;
    assert(pvr_txr_surface_yuv_input_size(&surface, PVR_TXR_YUV420,
                                          &byte_size) == -1);
    assert(errno == ENOTSUP);

    errno = 0;
    assert(pvr_txr_surface_yuv_input_size(&surface,
                                          (pvr_txr_yuv_format_t)2,
                                          &byte_size) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_txr_surface_yuv_input_size(&surface, PVR_TXR_YUV420,
                                          NULL) == -1);
    assert(errno == EINVAL);
}

static void test_rejections(void) {
    pvr_txr_surface_t surface;
    pvr_txr_level_info_t info;

    errno = 0;
    assert(pvr_txr_surface_init(NULL, 8, 8, PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, false) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_txr_surface_init(&surface, 7, 8,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, false) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_txr_surface_init(&surface, 1000, 512,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_STRIDE, false) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_txr_surface_init(&surface, 64, 64,
                                PVR_TXR_SURFACE_PAL8BPP,
                                PVR_TXR_SURFACE_LINEAR, false) == -1);
    assert(errno == ENOTSUP);

    errno = 0;
    assert(pvr_txr_surface_init(&surface, 64, 64,
                                PVR_TXR_SURFACE_PAL4BPP,
                                PVR_TXR_SURFACE_VQ, false) == -1);
    assert(errno == ENOTSUP);

    errno = 0;
    assert(pvr_txr_surface_init(&surface, 64, 32,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, true) == -1);
    assert(errno == EINVAL);

    assert(pvr_txr_surface_init(&surface, 64, 64,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, false) == 0);
    errno = 0;
    assert(pvr_txr_surface_get_level(&surface, 1, &info) == -1);
    assert(errno == ERANGE);

    errno = 0;
    assert(pvr_txr_surface_get_level(&surface, 0, NULL) == -1);
    assert(errno == EINVAL);

    ++surface.byte_size;
    errno = 0;
    assert(pvr_txr_surface_get_level(&surface, 0, &info) == -1);
    assert(errno == EINVAL);
}

int main(void) {
    test_plain_layouts();
    test_mipmap_layouts();
    test_vq_layouts();
    test_yuv_input_layouts();
    test_rejections();
    puts("pvr texture layout tests passed");
    return 0;
}
