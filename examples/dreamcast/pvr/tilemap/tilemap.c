/* KallistiOS ##version##

   Bounded scrolling maps, atlas flips, clipping, and polygon-list routing.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <assert.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define COLUMNS 11u
#define ROWS 7u
#define STRIDE 13u
#define TILE_COUNT 6u
#define DRAW_CAPACITY 512u
#define VERTEX_CAPACITY 8192u

static uint16_t pixels[64 * 32];
static uint32_t indices[ROWS * STRIDE];
static pvr_sprite_cell_t cells[TILE_COUNT];
static pvr_tilemap_tile_t tiles[TILE_COUNT];
alignas(32) static pvr_vertex_t vertices[VERTEX_CAPACITY];
static pvr_tilemap_draw_t draws[DRAW_CAPACITY];
static const pvr_list_t lists[3] = {
    PVR_LIST_OP_POLY, PVR_LIST_PT_POLY, PVR_LIST_TR_POLY
};

static void build_map(void) {
    static const uint16_t colors[6] = {
        0xff32, 0xffc2, 0xf3f3, 0xf3ff, 0x835f, 0x8f3f
    };

    for(unsigned y = 0; y < 32; ++y) {
        for(unsigned x = 0; x < 64; ++x) {
            unsigned region = y / 16 * 4 + x / 16;
            unsigned u = x % 16, v = y % 16;
            uint16_t color = region < 6 ? colors[region] : 0;
            /* An asymmetric white L and a black upper-right dot expose
               all UV flips. Punch-through cells have a transparent hole. */
            if((u == 2 && v >= 2 && v <= 12) ||
               (v == 12 && u >= 2 && u <= 9))
                color = (color & 0xf000) | 0x0fff;
            if(u >= 11 && u <= 13 && v >= 2 && v <= 4)
                color &= 0xf000;
            if(region / 2 == 1 && u >= 6 && u <= 10 && v >= 5 && v <= 9)
                color = 0;
            pixels[y * 64 + x] = color;
        }
    }
    for(unsigned i = 0; i < TILE_COUNT; ++i) {
        float u = (float)(i % 4) / 4;
        float v = (float)(i / 4) / 2;
        cells[i] = (pvr_sprite_cell_t){ 16, 16, 0, 0, u, v, u + .25f, v + .5f };
        tiles[i] = (pvr_tilemap_tile_t){
            .atlas_cell_index = i, .flags = i % 4,
            .material_id = i / 2, .priority = (int32_t)i - 3,
            .list = lists[i / 2],
            .argb = { 0xffffffff, 0xffffdddd, 0xffddffff, 0xffddddff }
        };
    }
    for(unsigned y = 0; y < ROWS; ++y)
        for(unsigned x = 0; x < STRIDE; ++x)
            indices[y * STRIDE + x] = x < COLUMNS ?
                (x + 3 * y) % TILE_COUNT : UINT32_C(0xdeadbeef);
    indices[3 * STRIDE + 5] = PVR_TILEMAP_EMPTY;
}

static int compare_draws(const void *left, const void *right) {
    const pvr_tilemap_draw_t *a = left, *b = right;
    if(a->priority != b->priority)
        return a->priority < b->priority ? -1 : 1;
    return (a->first_vertex > b->first_vertex) -
           (a->first_vertex < b->first_vertex);
}

int main(void) {
    const pvr_init_params_t params = {
        .opb_sizes = { PVR_BINSIZE_16, 0, PVR_BINSIZE_16, 0, PVR_BINSIZE_16 },
        .vertex_buf_size = 512 * 1024,
        .autosort_disabled = 1,
        .opb_overflow_count = 1
    };
    pvr_tilemap_t map = {
        .indices = indices, .index_count = ROWS * STRIDE,
        .columns = COLUMNS, .rows = ROWS, .row_stride = STRIDE,
        .tiles = tiles, .tile_count = TILE_COUNT,
        .atlas = { cells, TILE_COUNT }, .tile_width = 64, .tile_height = 48
    };
    pvr_tilemap_view_t view = {
        .left = 40, .top = 64, .right = 600, .bottom = 416,
        .anchor_x = 320, .anchor_y = 240,
        .scale_x = 1, .scale_y = 1, .depth = .5f,
        .max_candidates = DRAW_CAPACITY
    };
    pvr_material_t materials[3];
    pvr_pipeline_status_t pipeline;

    build_map();
    assert(pvr_init(&params) == 0);
    pvr_set_bg_color(.04f, .04f, .09f);
    assert(pvr_set_punch_through_alpha(128) == 0);
    pvr_ptr_t texture = pvr_mem_malloc(sizeof(pixels));
    assert(texture);
    assert(pvr_txr_load_ex_checked(pixels, texture, 64, 32,
                                   PVR_TXRLOAD_16BPP) == 0);
    for(unsigned i = 0; i < 3; ++i) {
        pvr_poly_cxt_t context;
        pvr_poly_cxt_txr(&context, lists[i], PVR_TXRFMT_ARGB4444,
                        64, 32, texture, PVR_FILTER_NONE);
        context.gen.culling = PVR_CULLING_NONE;
        assert(pvr_material_compile_polygon(&materials[i], &context, 0) == 0);
    }
    for(unsigned frame = 0; frame < 360; ++frame) {
        pvr_tilemap_result_t result;
        unsigned phase = frame / 90;
        view.address_x = phase == 3 ? PVR_TILEMAP_WRAP : phase;
        view.address_y = phase == 3 ? PVR_TILEMAP_CLIP : phase;
        view.scroll_x = (float)frame * .61f - 48;
        view.scroll_y = (float)frame * .37f - 24;
        view.rotation = .32f * sinf((float)frame * .017f);
        view.scale_x = .95f + .13f * cosf((float)frame * .023f);
        view.scale_y = .9f + .12f * sinf((float)frame * .019f);
        assert(pvr_tilemap_compile(vertices, VERTEX_CAPACITY, draws,
                                  DRAW_CAPACITY, &map, &view, &result) == 0);
        if(frame % 90 == 0)
            printf("tilemap phase %u: %u candidates, %u tiles, %u vertices\n",
                   phase, (unsigned)result.candidate_tiles,
                   (unsigned)result.visible_tiles, (unsigned)result.vertex_count);
        /* Sorting belongs to the caller. Keep stable ties without moving
           vertex packets; list boundaries still take precedence. */
        qsort(draws, result.visible_tiles, sizeof(*draws), compare_draws);
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        for(unsigned list = 0; list < 3; ++list) {
            pvr_geometry_vertex_sink_t sink;
            assert(pvr_list_begin(lists[list]) == 0);
            assert(pvr_material_submit(&materials[list]) == 0);
            assert(pvr_geometry_vertex_sink_init_current(
                &sink, PVR_GEOMETRY_VERTEX_CANONICAL) == 0);
            for(size_t i = 0; i < result.visible_tiles; ++i) {
                const pvr_tilemap_draw_t *draw = draws + i;
                if(draw->list != lists[list])
                    continue;
                assert(draw->material_id == list);
                assert(pvr_geometry_vertex_sink_emit(
                    &sink, vertices + draw->first_vertex, draw->vertex_count) == 0);
            }
            assert(pvr_list_finish() == 0);
        }
        assert(pvr_scene_finish() == 0);
    }
    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&pipeline) == 0);
    assert(pipeline.faults.mask == PVR_FAULT_NONE);
    puts("RESULT: PASS (tilemap scroll, clip, wrap, clamp, material routing)");
    /* Hold the final transformed map for inspection before releasing VRAM. */
    thd_sleep(10000);
    pvr_mem_free(texture);
    assert(pvr_shutdown() == 0);
    return 0;
}
