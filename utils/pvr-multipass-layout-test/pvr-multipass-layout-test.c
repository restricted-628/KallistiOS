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

static void test_frame_layout(void) {
    pvr_ta_frame_layout_t frame;

    assert(pvr_ta_frame_layout_calculate(&frame, 0, 0x00400000,
                                         0x1000, 0x200, 0x200, 1, 30,
                                         0x1000) == 0);
    assert(frame.vertex == 0);
    assert(frame.vertex_size == 0x1000);
    assert(frame.opb == 0x1000);
    assert(frame.opb_size == 0x200);
    assert(frame.opb_end == 0x1400);
    assert(frame.tile_matrix == 0x1448);
    assert(frame.tile_matrix_size == 120);
    assert(frame.frame == 0x1500);
    assert(frame.frame_size == 0x1000);
    assert(frame.bank_end == 0x2500);

    assert(pvr_ta_frame_layout_calculate(&frame, 0x00400000,
                                         0x00400000, 0x1000,
                                         0x200, 0x200, 1, 30, 0x1000) == 0);
    assert(frame.vertex == 0x00400000);
    assert(frame.opb == 0x00401000);
    assert(frame.tile_matrix == 0x00401448);
    assert(frame.frame == 0x00401500);
    assert(frame.bank_end == 0x00402500);
}

static void test_depth_and_writeout_control_bits(void) {
    /* Enumerate every depth-clear mask, including pass zero, for 1..8
       passes. Compare all words against legacy output, not just bit 30:
       pointers, sort, tile retention, coordinates and final marker must
       remain untouched. Multiple rows/columns catch cross-tile inheritance. */
    for(size_t count = 1; count <= PVR_MULTIPASS_MAX_PASSES; ++count) {
        pvr_ta_pass_layout_t passes[PVR_MULTIPASS_MAX_PASSES] = { 0 };
        pvr_ta_layout_t layout;
        uint32_t original[6 + 6 * 6 * PVR_MULTIPASS_MAX_PASSES];
        uint32_t changed[6 + 6 * 6 * PVR_MULTIPASS_MAX_PASSES];
        for(size_t pass = 0; pass < count; ++pass) {
            passes[pass].opb_size[pass % 5] = 32;
            passes[pass].presort = (pass & 1) != 0;
        }
        assert(pvr_ta_layout_calculate(&layout, 2, 3, passes, count) == 0);
        assert(pvr_ta_layout_build_regions(original, layout.region_words,
                                           0x4000, &layout, passes) == 0);
        for(unsigned mask = 0; mask < (1u << count); ++mask) {
            for(size_t pass = 0; pass < count; ++pass)
                passes[pass].clear_depth = (mask & (1u << pass)) != 0;
            assert(pvr_ta_layout_build_regions(changed, layout.region_words,
                                               0x4000, &layout, passes) == 0);
            for(size_t word = 0; word < layout.region_words; ++word) {
                uint32_t expected = original[word];
                if(word >= 6 && word % 6 == 0) {
                    const size_t pass = (word / 6 - 1) % count;
                    if(mask & (1u << pass))
                        expected &= ~UINT32_C(0x40000000);
                }
                assert(changed[word] == expected);
            }
        }
    }
}

static void test_shared_modifiers_and_compact_budget(void) {
    pvr_ta_pass_layout_t passes[2] = {
        { .opb_size = {32, 32, 64, 0, 0} },
        { .opb_size = {0, 0, 0, 0, 32}, .presort = true }
    };
    pvr_ta_layout_t original, shared;
    uint32_t a[54], b[54];
    assert(pvr_ta_layout_calculate(&original, 2, 2, passes, 2) == 0);
    passes[0].share_modifier = true;
    assert(pvr_ta_layout_calculate(&shared, 2, 2, passes, 2) == 0);
    assert(shared.total_opb_size == 640 && shared.largest_pass_opb_size == 512);
    assert(shared.region_words == 54);
    assert(!memcmp(shared.pass_opb_offset, original.pass_opb_offset,
                   sizeof(shared.pass_opb_offset)));
    for(unsigned bank = 0; bank <= 0x400000; bank += 0x400000) {
        pvr_ta_frame_layout_t f, compact;
        assert(pvr_ta_frame_layout_calculate(&f, bank, 0x400000, 4096,
                   640, 640, 3, 54, 4096) == 0);
        assert(pvr_ta_frame_layout_calculate(&compact, bank, 0x400000, 4096,
                   640, 512, 3, 54, 4096) == 0);
        assert(compact.opb == f.opb && f.opb_size == compact.opb_size);
        assert(compact.opb_end == compact.opb + 640 + 3 * 512);
        assert(f.opb_end == f.opb + 640 + 3 * 640);
        assert(compact.bank_end + 384 == f.bank_end);
        assert(compact.tile_matrix - PVR_REGION_HEADER_BYTES == compact.opb_end);

        const uint32_t limit = compact.bank_end - bank;
        assert(pvr_ta_frame_layout_calculate(&f, bank, limit, 4096,
                   640, 640, 3, 54, 4096) == -1 && errno == ENOSPC);
        assert(pvr_ta_frame_layout_calculate(&f, bank, limit, 4096,
                   640, 512, 3, 54, 4096) == 0);
        assert(pvr_ta_frame_layout_calculate(&f, bank, limit - 1, 4096,
                   640, 512, 3, 54, 4096) == -1 && errno == ENOSPC);

        passes[0].share_modifier = false;
        assert(pvr_ta_layout_build_regions(a, 54, compact.opb, &original, passes) == 0);
        passes[0].share_modifier = true;
        assert(pvr_ta_layout_build_regions(b, 54, compact.opb, &shared, passes) == 0);
        for(unsigned i = 0; i < 54; ++i) {
            if(i >= 6 && (i - 6) % 12 == 4) {
                assert(a[i] == EMPTY && b[i] == a[i-2] && b[i] != EMPTY);
            }
            else
                assert(b[i] == a[i]);
        }
    }
    for(unsigned i = 0; i < 4; ++i) {
        pvr_ta_pass_layout_t bad = passes[0];
        if(i == 0) bad.presort = true;
        if(i == 1) bad.opb_size[1] = 0;
        if(i == 2) bad.opb_size[2] = 0;
        if(i == 3) bad.opb_size[3] = 32;
        assert(pvr_ta_layout_calculate(&shared, 2, 2, &bad, 1) == -1 && errno == EINVAL);
    }
    pvr_ta_frame_layout_t f;
    assert(pvr_ta_frame_layout_calculate(&f, 0, 0x400000, 4096, 640, 0,
               0, 54, 4096) == -1 && errno == EINVAL);
    assert(pvr_ta_frame_layout_calculate(&f, 0, 0x400000, 4096, 640, 641,
               1, 54, 4096) == -1 && errno == EINVAL);
    assert(pvr_ta_frame_layout_calculate(&f, 0, 0x400000, 4096, 640, 512,
               UINT32_MAX, 54, 4096) == -1 && errno == EOVERFLOW);
    assert(pvr_ta_frame_layout_calculate(&f, 0, 0x400000, 4096, 640, 512,
               0, 54, 4096) == 0 && f.opb_end == f.opb + 640);
}

static void test_rejections(void) {
    pvr_ta_pass_layout_t pass = {
        .opb_size = { 32, 0, 0, 0, 0 }
    };
    pvr_ta_frame_layout_t frame;
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

    errno = 0;
    assert(pvr_ta_frame_layout_calculate(NULL, 0, 0x400000,
                                         0x1000, 0x200, 0x200, 1, 12,
                                         0x1000) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_ta_frame_layout_calculate(&frame, 0, 0x1000,
                                         0x1000, 0x200, 0x200, 1,
                                         12, 0x1000) == -1);
    assert(errno == ENOSPC);

    errno = 0;
    assert(pvr_ta_frame_layout_calculate(&frame, UINT32_MAX - 0x100, 0x200,
                                         0x1000, 0x200, 0x200, 1, 12,
                                         0x1000) == -1);
    assert(errno == EOVERFLOW);
}

int main(void) {
    test_one_pass_compatibility();
    test_two_pass_controls_and_offsets();
    test_three_pass_tile_order();
    test_maximum_pass_count();
    test_frame_layout();
    test_depth_and_writeout_control_bits();
    test_shared_modifiers_and_compact_budget();
    test_rejections();
    puts("pvr multipass layout tests passed");
    return 0;
}
