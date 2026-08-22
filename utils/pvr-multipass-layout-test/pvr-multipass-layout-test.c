/* KallistiOS ##version##

   Host-side PVR multipass layout tests.
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pvr_multipass_layout.h"

#define EMPTY UINT32_C(0x80000000)

static void test_one_pass_compatibility(void) {
    static const pvr_ta_pass_layout_t pass = {
        .opb_size = { 32, 0, 64, 0, 128 },
        .presort = false
    };
    static const uint32_t expected[30] = {
        0x10000000, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
        0x00000000, 0x00002000, EMPTY, 0x00002080, EMPTY, 0x00002180,
        0x00000100, 0x00002040, EMPTY, 0x00002100, EMPTY, 0x00002280,
        0x00000004, 0x00002020, EMPTY, 0x000020c0, EMPTY, 0x00002200,
        0x80000104, 0x00002060, EMPTY, 0x00002140, EMPTY, 0x00002300
    };
    pvr_ta_layout_t layout;
    uint32_t regions[30];

    assert(pvr_ta_layout_calculate(&layout, 2, 2, &pass, 1) == 0);
    assert(layout.tile_count == 4);
    assert(layout.pass_opb_offset[0] == 0);
    assert(layout.list_opb_offset[0][0] == 0);
    assert(layout.list_opb_offset[0][1] == 128);
    assert(layout.list_opb_offset[0][2] == 128);
    assert(layout.list_opb_offset[0][3] == 384);
    assert(layout.list_opb_offset[0][4] == 384);
    assert(layout.total_opb_size == 896);
    assert(layout.region_words == 30);
    assert(pvr_ta_layout_build_regions(regions, 30, 0x2000,
                                       &layout, &pass) == 0);
    assert(memcmp(regions, expected, sizeof(expected)) == 0);

    assert(regions[0] == 0x10000000);
    assert(regions[1] == EMPTY && regions[5] == EMPTY);

    /* KOS region ordering is column-major while list-pointer tile numbers
       remain row-major. */
    assert(regions[6] == 0x00000000);
    assert(regions[7] == 0x00002000);
    assert(regions[8] == EMPTY);
    assert(regions[9] == 0x00002080);
    assert(regions[10] == EMPTY);
    assert(regions[11] == 0x00002180);

    assert(regions[12] == 0x00000100);
    assert(regions[13] == 0x00002040);
    assert(regions[15] == 0x00002100);
    assert(regions[17] == 0x00002280);

    assert(regions[18] == 0x00000004);
    assert(regions[19] == 0x00002020);
    assert(regions[21] == 0x000020c0);
    assert(regions[23] == 0x00002200);

    assert(regions[24] == 0x80000104);
    assert(regions[25] == 0x00002060);
    assert(regions[27] == 0x00002140);
    assert(regions[29] == 0x00002300);
}

static void test_two_pass_controls_and_offsets(void) {
    static const pvr_ta_pass_layout_t passes[2] = {
        { .opb_size = { 32, 0, 0, 0, 0 }, .presort = false },
        { .opb_size = { 0, 0, 64, 0, 0 }, .presort = true }
    };
    pvr_ta_layout_t layout;
    uint32_t regions[18];

    assert(pvr_ta_layout_calculate(&layout, 1, 1, passes, 2) == 0);
    assert(layout.pass_opb_offset[0] == 0);
    assert(layout.pass_opb_offset[1] == 32);
    assert(layout.total_opb_size == 96);
    assert(layout.region_words == 18);
    assert(pvr_ta_layout_build_regions(regions, 18, 0x4000,
                                       &layout, passes) == 0);

    assert(regions[6] == 0x10000000);
    assert(regions[7] == 0x00004000);
    assert(regions[8] == EMPTY && regions[11] == EMPTY);

    assert(regions[12] == 0xe0000000);
    assert(regions[13] == EMPTY && regions[14] == EMPTY);
    assert(regions[15] == 0x00004020);
    assert(regions[16] == EMPTY && regions[17] == EMPTY);
}

static void test_three_pass_tile_order(void) {
    static const pvr_ta_pass_layout_t passes[3] = {
        { .opb_size = { 32, 0, 0, 0, 0 }, .presort = false },
        { .opb_size = { 0, 0, 32, 0, 0 }, .presort = true },
        { .opb_size = { 0, 0, 0, 0, 32 }, .presort = false }
    };
    pvr_ta_layout_t layout;
    uint32_t regions[42];

    assert(pvr_ta_layout_calculate(&layout, 2, 1, passes, 3) == 0);
    assert(pvr_ta_layout_build_regions(regions, 42, 0x8000,
                                       &layout, passes) == 0);

    assert(regions[6] == 0x10000000);
    assert(regions[12] == 0x70000000);
    assert(regions[18] == 0x40000000);
    assert(regions[24] == 0x10000004);
    assert(regions[30] == 0x70000004);
    assert(regions[36] == 0xc0000004);

    assert(regions[7] == 0x00008000);
    assert(regions[15] == 0x00008040);
    assert(regions[23] == 0x00008080);
    assert(regions[25] == 0x00008020);
    assert(regions[33] == 0x00008060);
    assert(regions[41] == 0x000080a0);
}

static void test_maximum_pass_count(void) {
    pvr_ta_pass_layout_t passes[PVR_MULTIPASS_MAX_PASSES] = { 0 };
    pvr_ta_layout_t layout;
    size_t pass;

    for(pass = 0; pass < PVR_MULTIPASS_MAX_PASSES; ++pass)
        passes[pass].opb_size[pass % 5] = 32;

    assert(pvr_ta_layout_calculate(&layout, 40, 15, passes,
                                   PVR_MULTIPASS_MAX_PASSES) == 0);
    assert(layout.total_opb_size == 153600);
    assert(layout.region_words == 28806);
}

static void test_rejections(void) {
    pvr_ta_pass_layout_t pass = {
        .opb_size = { 32, 0, 0, 0, 0 }
    };
    pvr_ta_layout_t layout;
    uint32_t regions[12];

    errno = 0;
    assert(pvr_ta_layout_calculate(NULL, 1, 1, &pass, 1) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_ta_layout_calculate(&layout, 41, 1, &pass, 1) == -1);
    assert(errno == EINVAL);

    pass.opb_size[0] = 16;
    errno = 0;
    assert(pvr_ta_layout_calculate(&layout, 1, 1, &pass, 1) == -1);
    assert(errno == EINVAL);

    pass.opb_size[0] = 0;
    errno = 0;
    assert(pvr_ta_layout_calculate(&layout, 1, 1, &pass, 1) == -1);
    assert(errno == EINVAL);

    pass.opb_size[0] = 32;
    assert(pvr_ta_layout_calculate(&layout, 1, 1, &pass, 1) == 0);

    errno = 0;
    assert(pvr_ta_layout_build_regions(regions, 11, 0x2000,
                                       &layout, &pass) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_ta_layout_build_regions(regions, 12, 3,
                                       &layout, &pass) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_ta_layout_build_regions(regions, 12, 0x00fffff0,
                                       &layout, &pass) == -1);
    assert(errno == EINVAL);
}

int main(void) {
    test_one_pass_compatibility();
    test_two_pass_controls_and_offsets();
    test_three_pass_tile_order();
    test_maximum_pass_count();
    test_rejections();
    puts("pvr multipass layout tests passed");
    return 0;
}
