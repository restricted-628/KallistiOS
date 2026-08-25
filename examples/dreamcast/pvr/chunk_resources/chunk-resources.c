/* KallistiOS ##version##

   Caller-owned compact-model resource-binding example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/pvr_chunk_binding.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define CHUNK_VERTEX_HEADER(type, size) \
    ((uint32_t)(type) | ((uint32_t)(size) << 16))
#define CHUNK_POLYGON_HEADER(type, flags) \
    ((uint16_t)(type) | ((uint16_t)(flags) << 8))

#define TEXTURE_SIZE 64u

static const uint32_t model_vertices[] = {
    CHUNK_VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 13),
    UINT32_C(0x00040000),
    UINT32_C(0x43200000), UINT32_C(0x42f00000), UINT32_C(0),
    UINT32_C(0x43f00000), UINT32_C(0x42f00000), UINT32_C(0),
    UINT32_C(0x43200000), UINT32_C(0x43b40000), UINT32_C(0),
    UINT32_C(0x43f00000), UINT32_C(0x43b40000), UINT32_C(0),
    UINT32_C(0x000000ff)
};

static const uint16_t model_polygons[] = {
    CHUNK_POLYGON_HEADER(PVR_CHUNK_TEXTURE, PVR_MIPBIAS_NORMAL),
    UINT16_C(0x4007),
    PVR_CHUNK_STRIP_UV8, UINT16_C(14), UINT16_C(1), UINT16_C(4),
    UINT16_C(0), UINT16_C(0), UINT16_C(0),
    UINT16_C(1), UINT16_C(255), UINT16_C(0),
    UINT16_C(2), UINT16_C(0), UINT16_C(255),
    UINT16_C(3), UINT16_C(255), UINT16_C(255),
    UINT16_C(0x00ff)
};

static alignas(32) const matrix_t screen_identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
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
    pvr_chunk_model_t model = {
        .vertex_words = model_vertices,
        .vertex_word_count = sizeof(model_vertices) /
                             sizeof(model_vertices[0]),
        .polygon_words = model_polygons,
        .polygon_word_count = sizeof(model_polygons) /
                              sizeof(model_polygons[0]),
        .center = { 320.0f, 240.0f, 0.0f },
        .radius = 200.0f
    };
    pvr_chunk_model_view_t model_view;
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
    assert(pvr_chunk_model_open(&model, &model_view) == 0);

    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    assert(pvr_chunk_residency_binding_init(
        &material_binding, &residency, texture_binding, render_handle, 1,
        NULL, NULL, &context, PVR_GEOMETRY_SINK_CURRENT_LIST) == 0);
    assert(pvr_chunk_residency_binding_prepare_model(&material_binding,
                                                     &model_view) == 0);
    assert(pvr_geometry_sink_init_current(&sink) == 0);

    for(frame = 0; frame < 120u; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_chunk_model_emit(
            &model_view, &screen_identity, &sink, workspace, 4,
            pvr_chunk_residency_binding_begin_strip, NULL,
            &material_binding, &render_result) == 0);
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
