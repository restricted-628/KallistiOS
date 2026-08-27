/* KallistiOS ##version##

   Host-side compact-model draw-cache tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_cache.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))

static const uint32_t vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_NORMAL, 19),
    UINT32_C(0x00030000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x3f800000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x40000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x000000ff)
};

static const uint16_t polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, UINT16_C(2),
    UINT16_C(0x3344), UINT16_C(0xff22),
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const uint16_t unsupported_polygons[] = {
    PVR_CHUNK_STRIP_TWO_VOLUME, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

typedef struct callback_state {
    size_t begins;
    size_t resolves;
    size_t prepares;
    uint16_t fail_index;
} callback_state_t;

void pvr_mod_compile(pvr_mod_hdr_t *dst, pvr_list_t list, uint32_t mode,
                     uint32_t cull) {
    (void)dst;
    (void)list;
    (void)mode;
    (void)cull;
}

int pvr_prim(const void *data, size_t size) {
    (void)data;
    (void)size;
    return 0;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    (void)list;
    return pvr_prim(data, size);
}

static void identity(matrix_t *matrix) {
    const matrix_t value = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    memcpy(matrix, &value, sizeof(value));
}

static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    callback_state_t *state = data;

    assert(strip->vertex_count == 3);
    assert(strip->state.diffuse_argb == UINT32_C(0xff223344));
    ++state->begins;
    return 0;
}

static int resolve_vertex(uint16_t source_index,
                          pvr_deform_vertex_t *vertex, void *data) {
    callback_state_t *state = data;

    ++state->resolves;
    if(source_index == state->fail_index) {
        errno = ECANCELED;
        return -1;
    }
    vertex->position.x += 10.0f;
    return 0;
}

static int prepare_vertex(const pvr_chunk_render_state_t *state,
                          uint16_t source_index,
                          const pvr_deform_vertex_t *deformation,
                          pvr_vertex_t *vertex, void *data) {
    callback_state_t *callback = data;

    assert(state->diffuse_argb == UINT32_C(0xff223344));
    assert(deformation->normal.z == 1.0f);
    vertex->argb = UINT32_C(0xff000000) | source_index;
    ++callback->prepares;
    return 0;
}

static pvr_chunk_model_t make_model(const uint16_t *polygon_words,
                                    size_t polygon_word_count) {
    pvr_chunk_model_t model = {
        vertices, sizeof(vertices) / sizeof(vertices[0]),
        polygon_words, polygon_word_count,
        { 1.0f, 0.0f, 0.0f }, 2.0f
    };

    return model;
}

int main(void) {
    pvr_chunk_model_t model =
        make_model(polygons, sizeof(polygons) / sizeof(polygons[0]));
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_cache_requirements_t requirements;
    alignas(32) uint8_t storage[1024];
    pvr_chunk_model_cache_t cache;
    alignas(8) matrix_t matrix;
    alignas(32) pvr_vertex_t workspace[3];
    alignas(32) pvr_vertex_t output[3];
    pvr_geometry_sink_t sink;
    pvr_chunk_cache_result_t result;
    callback_state_t callbacks;
    pvr_chunk_model_cache_t malformed;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(plan_requirements.vertex_index_entries == 256);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);

    assert(pvr_chunk_model_cache_query(&plan, &requirements) == 0);
    assert(requirements.alignment == 32);
    assert(requirements.strip_count == 1);
    assert(requirements.vertex_count == 3);
    assert(requirements.maximum_strip_vertices == 3);
    assert(requirements.vertices_offset % 32u == 0);
    assert(requirements.deform_vertices_offset % 32u == 0);
    assert(requirements.bytes <= sizeof(storage));

    memset(&cache, 0x5a, sizeof(cache));
    errno = 0;
    assert(pvr_chunk_model_cache_build(&plan, storage,
                                       requirements.bytes - 1u,
                                       NULL, NULL, &cache) == -1);
    assert(errno == ENOSPC && cache.version == 0);

    assert(pvr_chunk_model_cache_build(&plan, storage, sizeof(storage),
                                       NULL, NULL, &cache) == 0);
    assert(cache.version == PVR_CHUNK_CACHE_VERSION);
    assert(cache.strip_count == 1 && cache.vertex_count == 3);
    assert(cache.vertices[0].x == 0.0f);
    assert(cache.vertices[1].x == 1.0f);
    assert(cache.vertices[2].x == 2.0f);
    assert(cache.deform_vertices[2].normal.z == 1.0f);
    assert(cache.source_indices[0] == 0);
    assert(cache.source_indices[1] == 1);
    assert(cache.source_indices[2] == 2);

    identity(&matrix);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_cache_emit(&cache, &matrix, &sink,
                                      workspace, 3, begin_strip,
                                      resolve_vertex, prepare_vertex,
                                      &callbacks, &result) == 0);
    assert(result.emitted_strips == 1 && result.emitted_vertices == 3);
    assert(sink.emitted_vertices == 3);
    assert(callbacks.begins == 1 && callbacks.resolves == 3 &&
           callbacks.prepares == 3);
    assert(output[0].x == 10.0f && output[1].x == 11.0f &&
           output[2].x == 12.0f);
    assert(output[0].z == 1.0f);
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);
    assert(output[2].argb == UINT32_C(0xff000002));

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = 1;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    errno = 0;
    assert(pvr_chunk_model_cache_emit(&cache, &matrix, &sink,
                                      workspace, 3, NULL,
                                      resolve_vertex, NULL,
                                      &callbacks, &result) == -1);
    assert(errno == ECANCELED && result.emitted_vertices == 0 &&
           sink.emitted_vertices == 0);

    malformed = cache;
    ((pvr_chunk_cached_strip_t *)malformed.strips)->reserved = 1;
    errno = 0;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_cache_emit(&malformed, &matrix, &sink,
                                      workspace, 3, NULL, NULL, NULL,
                                      NULL, &result) == -1);
    assert(errno == EILSEQ && result.emitted_vertices == 0);
    ((pvr_chunk_cached_strip_t *)malformed.strips)->reserved = 0;

    model = make_model(unsupported_polygons,
                       sizeof(unsupported_polygons) /
                       sizeof(unsupported_polygons[0]));
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    errno = 0;
    assert(pvr_chunk_model_cache_query(&plan, &requirements) == -1);
    assert(errno == ENOTSUP && requirements.bytes == 0);

    puts("pvr-chunk-cache-test: PASS");
    return 0;
}
