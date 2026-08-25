/* KallistiOS ##version##

   Checked caller-owned sprite-cell geometry example.
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
#define INSTANCE_COUNT 3u

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
    pvr_sprite_instance_t instances[INSTANCE_COUNT] = {
        {
            .cell_index = 0,
            .position = { 160.0f, 240.0f, 0.5f, 1.0f },
            .rotation = 0.0f,
            .scale_x = 1.0f,
            .scale_y = 1.0f,
            .flags = PVR_SPRITE_INSTANCE_NONE
        },
        {
            .cell_index = 1,
            .position = { 360.0f, 180.0f, 0.6f, 1.0f },
            .rotation = 0.0f,
            .scale_x = 1.0f,
            .scale_y = 1.0f,
            .flags = PVR_SPRITE_INSTANCE_FLIP_U
        },
        {
            .cell_index = 0,
            .position = { 480.0f, 330.0f, 0.7f, 1.0f },
            .rotation = 0.0f,
            .scale_x = 0.65f,
            .scale_y = 0.65f,
            .flags = PVR_SPRITE_INSTANCE_HIDDEN
        }
    };
    const pvr_sprite_instance_stream_t stream = {
        instances, INSTANCE_COUNT, sizeof(instances[0])
    };
    alignas(32) pvr_sprite_txr_t packets[INSTANCE_COUNT];
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

    for(frame = 0; frame < 180u; ++frame) {
        pvr_geometry_vertex_sink_t sink;

        instances[0].rotation = (float)frame * 0.025f;
        instances[1].rotation = (float)frame * -0.0125f;
        instances[2].flags = ((frame / 30u) & 1u) ?
            PVR_SPRITE_INSTANCE_FLIP_V : PVR_SPRITE_INSTANCE_HIDDEN;

        assert(pvr_sprite_batch_compile_2d(packets, INSTANCE_COUNT,
                                           &atlas, &stream, &batch) == 0);
        assert(batch.examined_instances == INSTANCE_COUNT);
        assert(batch.produced_sprites ==
               (instances[2].flags & PVR_SPRITE_INSTANCE_HIDDEN ? 2u : 3u));

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
                   "RESULT: PASS (checked sprite cells)");
    puts("RESULT: PASS (checked caller-owned sprite cells)");

    for(;;)
        thd_sleep(1000);
}
