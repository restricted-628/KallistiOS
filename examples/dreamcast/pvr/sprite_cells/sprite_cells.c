/* KallistiOS ##version##

   Checked caller-owned cell-sprite stream example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEXTURE_WIDTH 32u
#define TEXTURE_HEIGHT 16u
#define CELL_COUNT 3u

static uint16_t texture_pixels[TEXTURE_WIDTH * TEXTURE_HEIGHT];

static void build_atlas_texture(void) {
    size_t x;
    size_t y;

    for(y = 0; y < TEXTURE_HEIGHT; ++y) {
        for(x = 0; x < TEXTURE_WIDTH; ++x) {
            uint16_t pixel;

            if(x < TEXTURE_WIDTH / 2u)
                pixel = ((x ^ y) & 2u) ? UINT16_C(0xffff) :
                                               UINT16_C(0x07e0);
            else
                pixel = ((x + y) & 2u) ? UINT16_C(0xf81f) :
                                               UINT16_C(0x001f);
            texture_pixels[y * TEXTURE_WIDTH + x] = pixel;
        }
    }
}

int main(int argc, char **argv) {
    const pvr_sprite_cell_t cells[2] = {
        { 96.0f, 96.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 1.0f },
        { 128.0f, 64.0f, 0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 1.0f }
    };
    const pvr_sprite_atlas_t atlas = { cells, 2 };
    const pvr_cell_state_t base_cells[CELL_COUNT] = {
        {
            .atlas_cell_index = 0,
            .offset = { -160.0f, 0.0f, 0.0f, 0.0f },
            .rotation = 0.0f,
            .scale_x = 1.0f,
            .scale_y = 1.0f,
            .priority = 0,
            .flags = PVR_CELL_NONE,
            .material_id = 0,
            .argb = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX },
            .oargb = { 0, 0, 0, 0 }
        },
        {
            .atlas_cell_index = 1,
            .offset = { 40.0f, -60.0f, 0.1f, 0.0f },
            .rotation = 0.0f,
            .scale_x = 1.0f,
            .scale_y = 1.0f,
            .priority = 2,
            .flags = PVR_CELL_NONE,
            .material_id = 0,
            .argb = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX },
            .oargb = { 0, 0, 0, 0 }
        },
        {
            .atlas_cell_index = 0,
            .offset = { 160.0f, 90.0f, 0.2f, 0.0f },
            .rotation = 0.0f,
            .scale_x = 0.65f,
            .scale_y = 0.65f,
            .priority = 1,
            .flags = PVR_CELL_HIDDEN,
            .material_id = 0,
            .argb = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX },
            .oargb = { 0, 0, 0, 0 }
        }
    };
    const pvr_cell_key_t visibility_keys[3] = {
        {
            .time = 0.0f, .slot_index = 2,
            .fields = PVR_CELL_KEY_FLAGS,
            .value = { .flags = PVR_CELL_HIDDEN }
        },
        {
            .time = 1.0f, .slot_index = 2,
            .fields = PVR_CELL_KEY_FLAGS,
            .value = { .flags = PVR_CELL_NONE }
        },
        {
            .time = 2.0f, .slot_index = 2,
            .fields = PVR_CELL_KEY_FLAGS,
            .value = { .flags = PVR_CELL_FLIP_V }
        }
    };
    const pvr_cell_key_t face_keys[3] = {
        {
            .time = 0.0f, .slot_index = 1,
            .fields = PVR_CELL_KEY_FLAGS,
            .value = { .flags = PVR_CELL_NONE }
        },
        {
            .time = 1.0f, .slot_index = 1,
            .fields = PVR_CELL_KEY_FLAGS,
            .value = { .flags = PVR_CELL_FLIP_U }
        },
        {
            .time = 2.0f, .slot_index = 1,
            .fields = PVR_CELL_KEY_ROTATION,
            .value = { .rotation = 0.35f }
        }
    };
    const pvr_cell_stream_t stream_sources[2] = {
        { visibility_keys, 3, 0.0f, 3.0f, 1 },
        { face_keys, 3, 0.5f, 3.0f, 1 }
    };
    pvr_cell_stream_view_t stream_views[2];
    const pvr_cell_stream_list_t stream_list = {
        stream_views, 2
    };
    pvr_cell_sprite_t sprite = {
        .base_cells = base_cells,
        .cell_count = CELL_COUNT,
        .position = { 320.0f, 240.0f, 0.5f, 1.0f },
        .rotation = 0.0f,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .argb = UINT32_MAX,
        .oargb = UINT32_MAX
    };
    pvr_cell_state_t sampled[CELL_COUNT];
    pvr_cell_state_t sample_workspace[CELL_COUNT];
    pvr_cell_resolved_t resolved[CELL_COUNT];
    alignas(32) pvr_sprite_txr_t packets[CELL_COUNT];
    pvr_cell_sample_result_t sampled_result;
    pvr_cell_resolve_result_t resolved_result;
    pvr_sprite_batch_result_t batch;
    pvr_pipeline_status_t status;
    pvr_sprite_cxt_t context;
    pvr_material_t material;
    pvr_ptr_t texture;
    unsigned int frame;

    (void)argc;
    (void)argv;

    vid_clear(96, 0, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.04f, 0.04f, 0.10f);

    build_atlas_texture();
    texture = pvr_mem_malloc(sizeof(texture_pixels));
    assert(texture);
    assert(pvr_txr_load_ex_checked(texture_pixels, texture,
                                   TEXTURE_WIDTH, TEXTURE_HEIGHT,
                                   PVR_TXRLOAD_16BPP) == 0);

    pvr_sprite_cxt_txr(&context, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
                       TEXTURE_WIDTH, TEXTURE_HEIGHT, texture,
                       PVR_FILTER_BILINEAR);
    context.gen.culling = PVR_CULLING_NONE;
    assert(pvr_material_compile_sprite(&material, &context, 0) == 0);
    assert(pvr_cell_stream_open(&stream_sources[0], CELL_COUNT,
                                &stream_views[0]) == 0);
    assert(pvr_cell_stream_open(&stream_sources[1], CELL_COUNT,
                                &stream_views[1]) == 0);

    for(frame = 0; frame < 180u; ++frame) {
        pvr_geometry_vertex_sink_t sink;
        float stream_time = (float)frame * 0.05f;

        sprite.rotation = (float)frame * 0.01f;
        assert(pvr_cell_stream_list_sample(&sprite, &stream_list, stream_time,
                                           sampled, sample_workspace,
                                           CELL_COUNT, &sampled_result) == 0);
        assert(sampled_result.sampled_streams == 2);
        assert(pvr_cell_sprite_resolve(&sprite, sampled, CELL_COUNT, resolved,
                                       CELL_COUNT, &resolved_result) == 0);
        assert(pvr_cell_resolved_sort(resolved, CELL_COUNT) == 0);
        assert(pvr_cell_sprite_compile_2d(packets, CELL_COUNT, &atlas,
                                          resolved, CELL_COUNT, &batch) == 0);
        assert(batch.examined_instances == CELL_COUNT);
        assert(batch.produced_sprites == resolved_result.visible_cells);

        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_material_submit(&material) == 0);
        assert(pvr_geometry_vertex_sink_init_current(
            &sink, PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED) == 0);
        assert(pvr_geometry_vertex_sink_emit(&sink, packets,
                                             batch.produced_sprites) == 0);
        assert(sink.emitted_vertices == batch.produced_sprites);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    pvr_mem_free(texture);
    assert(pvr_shutdown() == 0);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (cell streams + SH4ZAM)");
    puts("RESULT: PASS (caller-owned cell streams + SH4ZAM paths)");

    for(;;)
        thd_sleep(1000);
}
