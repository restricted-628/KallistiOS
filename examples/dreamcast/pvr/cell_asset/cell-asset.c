/* KallistiOS ##version##

   Build-generated cell-sprite asset and material-routing example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/pvr_cell_asset.h>

#include <assert.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define ATLAS_WIDTH 64u
#define ATLAS_HEIGHT 32u
#define CELL_COUNT 6u
#define STREAM_COUNT 3u
#define KEY_COUNT 15u
#define MATERIAL_COUNT 3u

extern const pvr_sprite_atlas_t cell_asset_atlas;
extern const unsigned char cell_asset_data[];
extern const int cell_asset_size;

static uint16_t texture_pixels[ATLAS_WIDTH * ATLAS_HEIGHT];

static const pvr_list_t material_lists[MATERIAL_COUNT] = {
    PVR_LIST_OP_POLY,
    PVR_LIST_PT_POLY,
    PVR_LIST_TR_POLY
};

static uint16_t pack_argb4444(unsigned int alpha, unsigned int red,
                              unsigned int green, unsigned int blue) {
    return (uint16_t)(((alpha & 15u) << 12) | ((red & 15u) << 8) |
                      ((green & 15u) << 4) | (blue & 15u));
}

static void build_atlas_texture(void) {
    static const uint8_t color[8][4] = {
        { 15, 15, 3, 2 },
        { 15, 15, 12, 2 },
        { 15, 3, 15, 3 },
        { 15, 3, 15, 15 },
        { 8, 3, 5, 15 },
        { 8, 15, 3, 15 },
        { 0, 0, 0, 0 },
        { 0, 0, 0, 0 }
    };
    size_t x;
    size_t y;

    for(y = 0; y < ATLAS_HEIGHT; ++y) {
        for(x = 0; x < ATLAS_WIDTH; ++x) {
            size_t region = (y / 16u) * 4u + x / 16u;
            unsigned int alpha = color[region][0];

            if((region == 2u || region == 3u) &&
               (((x / 4u) ^ (y / 4u)) & 1u))
                alpha = 0;
            texture_pixels[y * ATLAS_WIDTH + x] = pack_argb4444(
                alpha, color[region][1], color[region][2], color[region][3]);
        }
    }
}

static size_t select_material(const pvr_cell_resolved_t *cells,
                              size_t cell_count, uint32_t material_id,
                              pvr_cell_resolved_t *output) {
    size_t selected = 0;
    size_t index;

    for(index = 0; index < cell_count; ++index) {
        if(cells[index].material_id == material_id)
            output[selected++] = cells[index];
    }
    return selected;
}

int main(int argc, char **argv) {
    const pvr_init_params_t pvr_params = {
        .opb_sizes = {
            PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16,
            PVR_BINSIZE_0, PVR_BINSIZE_16
        },
        .vertex_buf_size = 512 * 1024,
        .opb_overflow_count = 1
    };
    pvr_cell_asset_view_t asset_view;
    pvr_cell_asset_runtime_t runtime;
    pvr_cell_state_t base_cells[CELL_COUNT];
    pvr_cell_key_t keys[KEY_COUNT];
    pvr_cell_stream_view_t streams[STREAM_COUNT];
    pvr_cell_sprite_t sprite;
    pvr_cell_state_t sampled[CELL_COUNT];
    pvr_cell_state_t sample_workspace[CELL_COUNT];
    pvr_cell_resolved_t resolved[CELL_COUNT];
    pvr_cell_resolved_t routed[CELL_COUNT];
    alignas(32) pvr_vertex_t packets[CELL_COUNT * 4u];
    pvr_material_t materials[MATERIAL_COUNT];
    pvr_pipeline_status_t pipeline;
    pvr_ptr_t texture;
    unsigned int frame;
    size_t index;

    (void)argc;
    (void)argv;

    assert(cell_asset_size > 0);
    assert(pvr_cell_asset_open(cell_asset_data, (size_t)cell_asset_size,
                               &asset_view) == 0);
    assert(asset_view.cell_count == CELL_COUNT);
    assert(asset_view.stream_count == STREAM_COUNT);
    assert(asset_view.key_count == KEY_COUNT);
    assert(pvr_cell_asset_materialize(
        &asset_view, base_cells, CELL_COUNT, keys, KEY_COUNT,
        streams, STREAM_COUNT, &runtime) == 0);
    assert(runtime.cell_count == CELL_COUNT);
    assert(runtime.stream_list.stream_count == STREAM_COUNT);
    assert(cell_asset_atlas.cell_count == 6u);

    sprite.base_cells = runtime.base_cells;
    sprite.cell_count = runtime.cell_count;
    sprite.position = (point_t){ 320.0f, 240.0f, 0.5f, 1.0f };
    sprite.rotation = 0.0f;
    sprite.scale_x = 1.0f;
    sprite.scale_y = 1.0f;
    sprite.argb = UINT32_MAX;
    sprite.oargb = UINT32_MAX;

    vid_clear(96, 0, 0);
    assert(pvr_init(&pvr_params) == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.06f);
    assert(pvr_set_punch_through_alpha(128) == 0);

    build_atlas_texture();
    texture = pvr_mem_malloc(sizeof(texture_pixels));
    assert(texture);
    assert(pvr_txr_load_ex_checked(texture_pixels, texture,
                                   ATLAS_WIDTH, ATLAS_HEIGHT,
                                   PVR_TXRLOAD_16BPP) == 0);

    for(index = 0; index < MATERIAL_COUNT; ++index) {
        pvr_poly_cxt_t context;

        pvr_poly_cxt_txr(&context, material_lists[index],
                         PVR_TXRFMT_ARGB4444, ATLAS_WIDTH, ATLAS_HEIGHT,
                         texture, PVR_FILTER_NONE);
        context.gen.culling = PVR_CULLING_NONE;
        assert(pvr_material_compile_polygon(&materials[index], &context,
                                            0) == 0);
    }

    for(frame = 0; frame < 240u; ++frame) {
        pvr_cell_sample_result_t sample_result;
        pvr_cell_resolve_result_t resolve_result;
        size_t produced_total = 0;
        float time = (float)frame / 60.0f;

        sprite.rotation = 0.06f * sinf(time * 1.5f);
        assert(pvr_cell_stream_list_sample(
            &sprite, &runtime.stream_list, time, sampled, sample_workspace,
            CELL_COUNT, &sample_result) == 0);
        assert(sample_result.sampled_streams == STREAM_COUNT);
        assert(sample_result.published_cells == CELL_COUNT);
        assert(pvr_cell_sprite_resolve(
            &sprite, sampled, CELL_COUNT, resolved, CELL_COUNT,
            &resolve_result) == 0);
        assert(resolve_result.resolved_cells == CELL_COUNT);
        assert(pvr_cell_resolved_sort(resolved, CELL_COUNT) == 0);
        for(index = 0; index < CELL_COUNT; ++index)
            assert(resolved[index].material_id < MATERIAL_COUNT);

        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();

        for(index = 0; index < MATERIAL_COUNT; ++index) {
            pvr_geometry_vertex_sink_t sink;
            pvr_sprite_batch_result_t batch;
            size_t selected = select_material(
                resolved, CELL_COUNT, (uint32_t)index, routed);

            assert(selected == 2u);
            assert(pvr_list_begin(material_lists[index]) == 0);
            assert(pvr_material_submit(&materials[index]) == 0);
            assert(pvr_cell_sprite_compile_colored_2d(
                packets, CELL_COUNT * 4u, &cell_asset_atlas,
                routed, selected, &batch) == 0);
            assert(batch.examined_instances == selected);
            assert(pvr_geometry_vertex_sink_init_current(
                &sink, PVR_GEOMETRY_VERTEX_CANONICAL) == 0);
            assert(pvr_geometry_vertex_sink_emit(
                &sink, packets, batch.produced_sprites * 4u) == 0);
            assert(sink.emitted_vertices == batch.produced_sprites * 4u);
            produced_total += batch.produced_sprites;
            assert(pvr_list_finish() == 0);
        }

        assert(produced_total == resolve_result.visible_cells);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&pipeline) == 0);
    assert(pipeline.faults.mask == PVR_FAULT_NONE);
    pvr_mem_free(texture);
    assert(pvr_shutdown() == 0);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (authored cell asset)");
    puts("RESULT: PASS (authored cell asset + material routing)");

    for(;;)
        thd_sleep(1000);
}
