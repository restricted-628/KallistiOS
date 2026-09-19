/* KallistiOS ##version##

   Topology-aware compact-model band-shading example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))

static const uint32_t model_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_NORMAL, 19),
    UINT32_C(0x00030000),
    /* Position (160, 360, 0), outward normal (-1, 1, 1). */
    UINT32_C(0x43200000), UINT32_C(0x43b40000), UINT32_C(0x00000000),
    UINT32_C(0xbf800000), UINT32_C(0x3f800000), UINT32_C(0x3f800000),
    /* Position (480, 360, 0), outward normal (1, 1, 1). */
    UINT32_C(0x43f00000), UINT32_C(0x43b40000), UINT32_C(0x00000000),
    UINT32_C(0x3f800000), UINT32_C(0x3f800000), UINT32_C(0x3f800000),
    /* Position (320, 100, 0), outward normal (0, -1, 1). */
    UINT32_C(0x43a00000), UINT32_C(0x42c80000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0xbf800000), UINT32_C(0x3f800000),
    UINT32_C(0x000000ff)
};

static const uint16_t model_polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, UINT16_C(2),
    UINT16_C(0xffff), UINT16_C(0xffff),
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const pvr_chunk_model_t model = {
    model_vertices, sizeof(model_vertices) / sizeof(model_vertices[0]),
    model_polygons, sizeof(model_polygons) / sizeof(model_polygons[0]),
    { 320.0f, 230.0f, 0.0f }, 230.0f
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
    static const float thresholds[] = { 0.0f };
    static const uint32_t band_colors[] = {
        UINT32_C(0xff203050), UINT32_C(0xffffd080)
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_cache_requirements_t cache_requirements;
    alignas(32) uint8_t cache_storage[512];
    pvr_chunk_model_cache_t cache;
    pvr_poly_cxt_t polygon_context;
    pvr_poly_hdr_t polygon_header;
    pvr_poly_hdr_t outline_header;
    pvr_normal_matrix_t normal_matrix;
    pvr_frustum_t frustum;
    pvr_geometry_sink_t sink;
    alignas(32) pvr_vertex_t vertices[3];
    alignas(32) pvr_deform_vertex_t deformations[3];
    vector_t normals[3];
    float shades[3];
    pvr_toon_triangle_t toon_triangles[3];
    alignas(32) pvr_vertex_t clip_vertices[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    pvr_chunk_toon_workspace_t workspace = {
        vertices, deformations, normals, shades, 3,
        toon_triangles, 3, clip_vertices, PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    pvr_chunk_outline_workspace_t outline_workspace = {
        vertices, deformations, 3,
        clip_vertices, PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    pvr_chunk_toon_profile_t profile;
    const pvr_chunk_outline_profile_t outline_profile = {
        12.0f, UINT32_C(0xff080c18), UINT32_C(0)
    };
    pvr_chunk_toon_result_t result;
    pvr_chunk_outline_result_t outline_result;
    pvr_pipeline_status_t status;
    unsigned frame;

    (void)argc;
    (void)argv;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    assert(pvr_chunk_model_cache_query(&plan, &cache_requirements) == 0);
    assert(cache_requirements.bytes <= sizeof(cache_storage));
    assert(pvr_chunk_model_cache_build(&plan, cache_storage,
                                       sizeof(cache_storage), NULL, NULL,
                                       &cache) == 0);
    assert(pvr_normal_matrix_build(&normal_matrix, &screen_identity) == 0);
    assert(pvr_frustum_init(&frustum, &screen_identity, 0.0f, 0.0f,
                            640.0f, 480.0f, 0.5f, 2.0f) == 0);

    memset(&profile, 0, sizeof(profile));
    profile.equation = PVR_TOON_SHADE_DOT;
    profile.thresholds = thresholds;
    profile.argb_modulation = band_colors;
    profile.threshold_count = 1;
    profile.epsilon = 1.0e-5f;

    vid_clear(96, 0, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);
    pvr_poly_cxt_col(&polygon_context, PVR_LIST_OP_POLY);
    polygon_context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&polygon_header, &polygon_context);
    polygon_context.depth.write = false;
    pvr_poly_compile(&outline_header, &polygon_context);
    assert(pvr_geometry_sink_init_current(&sink) == 0);

    for(frame = 0; frame < 240u; ++frame) {
        vector_t direction = {
            sinf((float)frame * 0.035f), 0.0f,
            cosf((float)frame * 0.035f), 0.0f
        };

        assert(pvr_toon_light_init(&profile.light, &direction,
                                   1.0f, 0.0f) == 0);
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_chunk_model_cache_emit_outline(
            &cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE,
            &outline_profile, &sink, &outline_workspace, NULL, begin_strip,
            NULL, NULL, NULL, &outline_header, &outline_result) == 0);
        assert(outline_result.emitted_strips == 1 &&
               outline_result.source_triangles == 1 &&
               outline_result.emitted_vertices == 3);
        assert(pvr_chunk_model_cache_emit_toon(
            &cache, &normal_matrix, &frustum,
            PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile, &sink, &workspace,
            NULL, begin_strip, NULL, NULL, NULL,
            &polygon_header, &result) == 0);
        assert(result.emitted_strips == 1 &&
               result.source_triangles == 1 &&
               result.emitted_vertices >= 3);
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
                   "RESULT: PASS (bands and outline shell)");
    puts("RESULT: PASS (bands and outline shell)");

    for(;;)
        thd_sleep(1000);
}
