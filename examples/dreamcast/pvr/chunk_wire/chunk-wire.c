/* KallistiOS ##version##

   Prepared compact-model wireframe example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))

static const uint32_t model_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_NORMAL, 25),
    UINT32_C(0x00040000),
    /* Left top. */
    UINT32_C(0x43340000), UINT32_C(0x430c0000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000),
    /* Left bottom. */
    UINT32_C(0x43340000), UINT32_C(0x43aa0000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000),
    /* Right top. */
    UINT32_C(0x43e60000), UINT32_C(0x430c0000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000),
    /* Right bottom. */
    UINT32_C(0x43e60000), UINT32_C(0x43aa0000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000),
    UINT32_C(0x000000ff)
};

static const uint16_t model_polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, UINT16_C(2),
    UINT16_C(0xffff), UINT16_C(0xffff),
    PVR_CHUNK_STRIP_INDEX, UINT16_C(6), UINT16_C(1),
    UINT16_C(4), UINT16_C(0), UINT16_C(1), UINT16_C(2), UINT16_C(3),
    UINT16_C(0x00ff)
};

static const pvr_chunk_model_t model = {
    model_vertices, sizeof(model_vertices) / sizeof(model_vertices[0]),
    model_polygons, sizeof(model_polygons) / sizeof(model_polygons[0]),
    { 320.0f, 250.0f, 0.0f }, 210.0f
};

static alignas(32) const matrix_t screen_identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    const pvr_poly_hdr_t *header = data;

    (void)strip;
    return pvr_prim(header, sizeof(*header));
}

int main(int argc, char **argv) {
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_cache_requirements_t requirements;
    alignas(32) uint8_t cache_storage[1024];
    pvr_chunk_model_cache_t cache;
    pvr_poly_cxt_t polygon_context;
    pvr_poly_hdr_t polygon_header;
    pvr_frustum_t frustum;
    pvr_geometry_sink_t sink;
    alignas(32) pvr_vertex_t vertices[4];
    alignas(32) pvr_deform_vertex_t deformations[4];
    pvr_chunk_wire_workspace_t workspace = {
        vertices, deformations, 4
    };
    pvr_chunk_wire_profile_t profile = {
        4.0f, UINT32_C(0xff80d8ff), UINT32_C(0),
        PVR_CHUNK_WIRE_MESH, PVR_CHUNK_WIRE_COLOR_PROFILE
    };
    pvr_chunk_wire_result_t result;
    pvr_pipeline_status_t status;
    unsigned frame;

    (void)argc;
    (void)argv;
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    assert(pvr_chunk_model_cache_query(&plan, &requirements) == 0);
    assert(requirements.bytes <= sizeof(cache_storage));
    assert(pvr_chunk_model_cache_build(&plan, cache_storage,
                                       sizeof(cache_storage), NULL, NULL,
                                       &cache) == 0);
    assert(pvr_frustum_init(&frustum, &screen_identity, 0.0f, 0.0f,
                            640.0f, 480.0f, 0.5f, 2.0f) == 0);

    vid_clear(96, 0, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.015f, 0.02f, 0.04f);
    pvr_poly_cxt_col(&polygon_context, PVR_LIST_OP_POLY);
    polygon_context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&polygon_header, &polygon_context);
    assert(pvr_geometry_sink_init_current(&sink) == 0);

    for(frame = 0; frame < 360u; ++frame) {
        profile.topology = (pvr_chunk_wire_topology_t)(frame / 120u);
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_chunk_model_cache_emit_wire(
            &cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE,
            &profile, &sink, &workspace, NULL, begin_strip,
            NULL, NULL, NULL, &polygon_header, &result) == 0);
        assert(result.emitted_edges > 0 && result.emitted_vertices ==
               result.emitted_edges * PVR_GEOMETRY_LINE_VERTICES);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    assert(pvr_shutdown() == 0);
    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (compact wireframe policies)");
    return 0;
}
