/* KallistiOS ##version##

   Authored compact-model opacity routing example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/pvr_chunk_binding.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

extern const unsigned char chunk_opacity_asset_data[];
extern const int chunk_opacity_asset_size;

static alignas(32) const matrix_t screen_identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

static const pvr_list_t pass_lists[] = {
    PVR_LIST_OP_POLY,
    PVR_LIST_PT_POLY,
    PVR_LIST_TR_POLY
};

int main(int argc, char **argv) {
    const pvr_init_params_t pvr_params = {
        .opb_sizes = {
            PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16,
            PVR_BINSIZE_0, PVR_BINSIZE_16
        },
        .vertex_buf_size = 512 * 1024,
        .opb_overflow_count = 1
    };
    const pvr_chunk_texture_table_view_t textures = { 0 };
    pvr_chunk_asset_view_t asset;
    pvr_chunk_asset_workspace_requirements_t asset_requirements;
    pvr_chunk_model_view_t model_view;
    pvr_chunk_model_plan_t model_plan;
    pvr_chunk_model_plan_requirements_t requirements;
    pvr_chunk_vertex_index_entry_t
        vertex_index[PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE];
    pvr_chunk_material_binding_t binding[3];
    pvr_poly_cxt_t context;
    pvr_geometry_sink_t sink;
    alignas(32) pvr_vertex_t workspace[4];
    pvr_pipeline_status_t status;
    void *asset_workspace = NULL;
    size_t asset_workspace_bytes = 0;
    uint8_t threshold;
    unsigned int frame;
    size_t pass;

    (void)argc;
    (void)argv;

    vid_clear(64, 0, 0);
    assert(pvr_init(&pvr_params) == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.05f);
    assert(pvr_set_punch_through_alpha(128) == 0);
    assert(pvr_get_punch_through_alpha(&threshold) == 0);
    assert(threshold == 128);

    assert(chunk_opacity_asset_size > 0);
    assert(pvr_chunk_asset_open(chunk_opacity_asset_data,
                                (size_t)chunk_opacity_asset_size,
                                &asset) == 0);
    assert(pvr_chunk_asset_workspace_query(&asset,
                                           &asset_requirements) == 0);
    if(asset_requirements.bytes) {
        asset_workspace_bytes =
            (asset_requirements.bytes + asset_requirements.alignment - 1u) &
            ~(asset_requirements.alignment - 1u);
        asset_workspace = aligned_alloc(asset_requirements.alignment,
                                        asset_workspace_bytes);
        assert(asset_workspace);
    }
    assert(pvr_chunk_asset_load(&asset, NULL, NULL, asset_workspace,
                                asset_workspace_bytes, &model_view) == 0);
    assert(model_view.info.strips == 3);
    assert(model_view.info.maximum_strip_vertices == 4);
    assert(pvr_chunk_model_plan_query(&model_view, &requirements) == 0);
    assert(requirements.vertex_index_entries <=
           sizeof(vertex_index) / sizeof(vertex_index[0]));
    assert(pvr_chunk_model_plan_build(
        &model_view, vertex_index,
        sizeof(vertex_index) / sizeof(vertex_index[0]), &model_plan) == 0);

    for(pass = 0; pass < 3; ++pass) {
        pvr_poly_cxt_col(&context, pass_lists[pass]);
        /* Keep this integration example independent of source winding. The
           material binding still applies authored double-sided state where it
           exists; opacity-list routing is the behavior under test here. */
        context.gen.culling = PVR_CULLING_NONE;
        assert(pvr_chunk_material_binding_init(
            &binding[pass], &context, &textures,
            PVR_GEOMETRY_SINK_CURRENT_LIST) == 0);
    }
    assert(pvr_geometry_sink_init_current(&sink) == 0);

    for(frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();

        for(pass = 0; pass < 3; ++pass) {
            pvr_chunk_render_result_t result;

            assert(pvr_list_begin(pass_lists[pass]) == 0);
            assert(pvr_chunk_model_emit_prepared_filtered(
                &model_plan, &screen_identity, &sink, workspace,
                sizeof(workspace) / sizeof(workspace[0]),
                pvr_chunk_material_binding_filter_strip,
                pvr_chunk_material_binding_begin_strip,
                NULL, &binding[pass], &result) == 0);
            assert(result.emitted_strips == 1);
            assert(result.emitted_vertices == 4);
            assert(pvr_list_finish() == 0);
        }

        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    vid_waitvbl();
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    assert(pvr_shutdown() == 0);
    free(asset_workspace);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (compact opacity routing)");
    puts("RESULT: PASS (compact opacity routing)");

    for(;;)
        thd_sleep(1000);
}
