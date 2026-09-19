/* KallistiOS ##version##

   Build-generated compact-model resource-binding example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/pvr_chunk_binding.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEXTURE_SIZE 64u

extern const pvr_chunk_model_t chunk_resource_model;

static alignas(32) const matrix_t screen_identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

static const pvr_light_t model_lights[] = {
    {
        .kind = PVR_LIGHT_DIRECTIONAL,
        .source.direction = { 0.0f, 0.0f, 1.0f, 0.0f },
        .color = { 1.0f, 1.0f, 1.0f, 0.0f },
        .intensity = 0.8f
    },
    {
        .kind = PVR_LIGHT_DIRECTIONAL,
        .source.direction = { 0.0f, 0.0f, 1.0f, 0.0f },
        .color = { 1.0f, 0.0f, 0.0f, 0.0f },
        .intensity = -0.1f
    }
};

static uint16_t texture_pixels[TEXTURE_SIZE * TEXTURE_SIZE];

static void build_texture(void) {
    size_t x;
    size_t y;

    for(y = 0; y < TEXTURE_SIZE; ++y) {
        for(x = 0; x < TEXTURE_SIZE; ++x) {
            uint16_t color = ((x >> 3) ^ (y >> 3)) & 1u ?
                             UINT16_C(0xffff) : UINT16_C(0x041f);

            texture_pixels[y * TEXTURE_SIZE + x] = color;
        }
    }
}

int main(int argc, char **argv) {
    pvr_chunk_model_view_t model_view;
    pvr_chunk_model_plan_t model_plan;
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_vertex_index_entry_t
        vertex_index[PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE];
    pvr_txr_residency_t residency;
    pvr_txr_residency_slot_t residency_slot[1];
    pvr_txr_surface_t residency_surface[1];
    pvr_txr_surface_t prototype;
    pvr_txr_surface_t *surface;
    pvr_txr_residency_handle_t upload_handle;
    pvr_chunk_texture_binding_t texture_binding[1];
    pvr_txr_residency_handle_t render_handle[1];
    pvr_poly_cxt_t context;
    pvr_chunk_residency_binding_t material_binding;
    pvr_lighting_extended_context_t lighting;
    pvr_chunk_render_policy_config_t policy_config;
    pvr_chunk_render_policy_binding_t policy_binding;
    pvr_geometry_sink_t sink;
    alignas(32) pvr_vertex_t workspace[4];
    pvr_chunk_render_result_t render_result;
    pvr_pipeline_status_t status;
    pvr_txr_residency_status_t residency_status;
    unsigned int frame;

    (void)argc;
    (void)argv;

    vid_clear(96, 0, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);

    build_texture();
    assert(pvr_txr_surface_init(&prototype, TEXTURE_SIZE, TEXTURE_SIZE,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(pvr_txr_residency_init(&residency, residency_slot,
                                  residency_surface, 1, &prototype) == 0);
    assert(pvr_txr_residency_reserve(&residency, 7, &upload_handle,
                                     &surface) == 0);
    assert(pvr_txr_surface_upload(surface, texture_pixels,
                                  sizeof(texture_pixels),
                                  PVR_TXR_TRANSFER_CPU) == 0);
    assert(pvr_txr_residency_publish(&residency, upload_handle) == 0);
    assert(pvr_txr_residency_unpin(&residency, upload_handle) == 0);
    assert(pvr_chunk_model_open(&chunk_resource_model, &model_view) == 0);
    assert(pvr_chunk_model_plan_query(&model_view, &plan_requirements) == 0);
    assert(plan_requirements.vertex_index_entries <=
           sizeof(vertex_index) / sizeof(vertex_index[0]));
    assert(pvr_chunk_model_plan_build(
        &model_view, vertex_index,
        sizeof(vertex_index) / sizeof(vertex_index[0]), &model_plan) == 0);
    assert(model_view.info.maximum_strip_vertices <=
           sizeof(workspace) / sizeof(workspace[0]));

    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    assert(pvr_chunk_residency_binding_init(
        &material_binding, &residency, texture_binding, render_handle, 1,
        NULL, NULL, &context, PVR_GEOMETRY_SINK_CURRENT_LIST) == 0);
    assert(pvr_chunk_residency_binding_prepare_model(&material_binding,
                                                     &model_view) == 0);
    memset(&lighting, 0, sizeof(lighting));
    lighting.ambient[0] = lighting.ambient[1] = lighting.ambient[2] = 0.2f;
    lighting.lights = model_lights;
    lighting.light_count = sizeof(model_lights) / sizeof(model_lights[0]);
    lighting.view_position.x = 320.0f;
    lighting.view_position.y = 240.0f;
    lighting.view_position.z = 10.0f;
    lighting.specular_exponent = 8.0f;

    memset(&policy_config, 0, sizeof(policy_config));
    policy_config.policy = PVR_CHUNK_RENDER_POLICY_DIFFUSE_SPECULAR;
    policy_config.object_to_world = &screen_identity;
    policy_config.lighting = &lighting;
    policy_config.begin_strip =
        pvr_chunk_residency_binding_begin_strip;
    policy_config.begin_strip_data = &material_binding;
    assert(pvr_chunk_render_policy_binding_init(
        &policy_binding, &policy_config) == 0);
    assert(pvr_geometry_sink_init_current(&sink) == 0);

    for(frame = 0; frame < 120u; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_chunk_model_emit_prepared(
            &model_plan, &screen_identity, &sink, workspace, 4,
            pvr_chunk_render_policy_binding_begin_strip,
            pvr_chunk_render_policy_binding_prepare_vertex,
            &policy_binding, &render_result) == 0);
        assert(render_result.emitted_strips == 1 &&
               render_result.emitted_vertices == 4);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_chunk_residency_binding_release(&material_binding) == 0);
    assert(pvr_txr_residency_get_status(&residency,
                                        &residency_status) == 0);
    assert(residency_status.ready_slots == 1
           && residency_status.pin_count == 0
           && residency_status.hits == 1
           && residency_status.misses == 0
           && residency_status.evictions == 0);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    assert(pvr_txr_residency_destroy(&residency) == 0);
    assert(pvr_shutdown() == 0);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (compact texture resources)");
    puts("RESULT: PASS (compact texture resources)");

    for(;;)
        thd_sleep(1000);
}
